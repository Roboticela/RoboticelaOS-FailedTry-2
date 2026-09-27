/*
 * RoboCSS selector matching against the real RoboHTML DOM (Phase 9.4) —
 * css.dom.*
 *
 * The CSS-side suites match through a host test double, which is what lets them
 * cover every combinator and every structural pseudo-class exhaustively without a
 * document. This suite is the other half: it implements the same seam over the
 * *actual* DOM the 8.x tree builder produces and re-runs the selectors through
 * it. A matcher that only worked against a hand-shaped fixture would pass those
 * suites and fail here, and the difference is exactly what this file is for.
 *
 * Three things only a real tree can prove, and all three are asserted below:
 *
 *   Text between elements is invisible to `+`, `~`, `:first-child` and
 *   `:nth-child`. The checkpoint document is written one element per line, so
 *   every pair of sibling elements has a text node between them. Sibling
 *   counting that counted *nodes* would find no adjacent pairs at all.
 *
 *   Selector matching does not disturb the document. Matching reads; the tree
 *   version, the node count and a fingerprint of every element's tag and id are
 *   asserted unchanged, and the production pipeline re-renders the document
 *   afterwards and must produce the same frame byte for byte.
 *
 *   The relations come from the DOM's own links rather than from a copy. After a
 *   structural mutation the host is rebuilt and the new shape is what matches,
 *   which is what makes the rebuild requirement real rather than decorative.
 *
 * The adapter below is the whole of the RoboCSS↔RoboHTML boundary, and it is
 * deliberately the only place in the runtime where the two subsystems meet.
 */

#include "RoboCssTestSupport.h"

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboGraphics/RoboGraphics.h"
/* `DomCheckpointHtml`: the document of record this suite's selector repertoire and element counts are
 * written against. It used to come from the deleted demo layer; it is a document fixture, so it lives
 * with the HTML suites' fixtures now. */
#include "RoboHTMLDomTestSupport.h"
/* And the production picture: `RenderSceneBegin` + `RenderRasterize`, the same chain a window
 * shows. The three `*_does_not_disturb_the_picture` cases compare *frames*, and this is where a frame
 * comes from now that the demo's painter is gone. */
#include "RoboRenderTestSupport.h"

/* ------------------------------------------------------------------ */
/* Small text helpers                                                 */
/* ------------------------------------------------------------------ */

/*
 * The host suite has no formatted output (the test framework deliberately does
 * not depend on stdio), so selectors and markup for generated fixtures are
 * assembled from pieces. Both helpers are bounds-checked rather than trusting
 * the caller's arithmetic.
 */

/** How many levels the deep-tree fixture nests. Deeper than any comfortable
 *  recursion budget, well inside the document's node ceiling. */
#define CSS_DOM_DEEP_LEVELS 96u

/** Write @p value in decimal (no padding) and NUL-terminate. Returns the digits. */
static size_t CssDomWriteUint(char *out, size_t capacity, unsigned value)
{
    char digits[16];
    size_t count = 0u;
    size_t i;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));

    RoboCheckEqInt(count + 1u <= capacity, 1);
    for (i = 0u; i < count; i++) {
        out[i] = digits[count - 1u - i];
    }
    out[count] = '\0';
    return count;
}

/** Write level @p level's id ("dN"). Returns its length; the result is NUL-free. */
static size_t CssDomWriteId(char *out, size_t capacity, unsigned level)
{
    out[0] = 'd';
    return CssDomWriteUint(out + 1u, capacity - 1u, level) + 1u;
}

/** Write the opening tag of level @p level. Returns the bytes written, no NUL. */
static size_t CssDomWriteDiv(char *out, size_t capacity, unsigned level)
{
    size_t at = 0u;

    RoboCheckEqInt(12u <= capacity, 1);
    memcpy(out, "<div id=\"", 9u);
    at = 9u;
    at += CssDomWriteId(out + at, capacity - at, level);
    out[at++] = '"';
    out[at++] = '>';
    return at;
}

/** `out = prefix + id + suffix`, bounds-checked. */
static void CssDomJoin(char *out,
                         size_t capacity,
                         const char *prefix,
                         const char *id,
                         const char *suffix)
{
    RoboCheckEqInt(strlen(prefix) + strlen(id) + strlen(suffix) + 1u <= capacity, 1);
    strcpy(out, prefix);
    strcat(out, id);
    strcat(out, suffix);
}

/* ------------------------------------------------------------------ */
/* The RoboHTML host adapter                                          */
/* ------------------------------------------------------------------ */

/*
 * Caps rather than truncation. An element with more classes or attributes than
 * this fails the fixture loudly, because a host that silently dropped the
 * eleventh attribute would turn `[data-x]` into a wrong answer instead of an
 * unsupported one. The numbers are above anything the checkpoint or the fixtures
 * use by a wide margin.
 */
#define CSS_DOM_MAX_CLASSES 8u
#define CSS_DOM_MAX_ATTRIBUTES 16u

typedef struct cssDomElement {
    HtmlNodeHandle handle;
    /* Element relations, resolved once at build time. */
    CssElementRef parent;
    CssElementRef previous;
    CssElementRef next;
    CssSlice tag; /* borrowed from the document */
    int hasId;
    CssSlice id; /* borrowed from the document */
    /* Owned arrays of borrowed slices: stable for the host's lifetime, which is
     * what makes CssElementView safe to fill for one element while the
     * matcher is still using another (`:not()` recurses). */
    CssSlice classes[CSS_DOM_MAX_CLASSES];
    size_t classCount;
    CssAttribute attributes[CSS_DOM_MAX_ATTRIBUTES];
    size_t attributeCount;
} CssDomElement;

typedef struct cssDomHost {
    HtmlDocumentId document;
    CssDomElement *elements; /* owned; ref is index + 1 */
    size_t count;
    size_t capacity;
    CssElementHost host;
} CssDomHost;

static Result CssDomViewOf(void *userdata,
                                     CssElementRef ref,
                                     CssElementView *out)
{
    CssDomHost *dom = (CssDomHost *)userdata;
    const CssDomElement *element;

    if (!dom || !out || ref == RoboCssElementRefNone || ref > dom->count) {
        return RoboNotFound;
    }
    element = &dom->elements[ref - 1u];
    CssElementViewInit(out);
    if (CssElementViewSetElement(out, ref, element->tag, NULL) != RoboOk) {
        return RoboError;
    }
    /* The id is recorded only when the attribute exists — which is the same
     * answer either way, since `#name` can never name an empty id. Recording the
     * distinction keeps the host honest about what the DOM said. */
    if (element->hasId &&
        CssElementViewSetId(out, element->id, NULL) != RoboOk) {
        return RoboError;
    }
    if (CssElementViewSetClasses(out, element->classes, element->classCount,
                                          NULL) != RoboOk) {
        return RoboError;
    }
    if (CssElementViewSetAttributes(out, element->attributes,
                                             element->attributeCount, NULL) != RoboOk) {
        return RoboError;
    }
    return RoboOk;
}

static CssElementRef CssDomParentOf(void *userdata,
                                                CssElementRef ref)
{
    CssDomHost *dom = (CssDomHost *)userdata;

    if (!dom || ref == RoboCssElementRefNone || ref > dom->count) {
        return RoboCssElementRefNone;
    }
    return dom->elements[ref - 1u].parent;
}

static CssElementRef CssDomPreviousOf(void *userdata,
                                                  CssElementRef ref)
{
    CssDomHost *dom = (CssDomHost *)userdata;

    if (!dom || ref == RoboCssElementRefNone || ref > dom->count) {
        return RoboCssElementRefNone;
    }
    return dom->elements[ref - 1u].previous;
}

static CssElementRef CssDomNextOf(void *userdata,
                                              CssElementRef ref)
{
    CssDomHost *dom = (CssDomHost *)userdata;

    if (!dom || ref == RoboCssElementRefNone || ref > dom->count) {
        return RoboCssElementRefNone;
    }
    return dom->elements[ref - 1u].next;
}

/** Ref of the element @p handle names, or NONE. */
static CssElementRef CssDomRefOfHandle(const CssDomHost *dom,
                                                    const HtmlNodeHandle *handle)
{
    size_t i;

    for (i = 0u; i < dom->count; i++) {
        if (HtmlNodeHandleEqual(&dom->elements[i].handle, handle)) {
            return (CssElementRef)(i + 1u);
        }
    }
    return RoboCssElementRefNone;
}

/*
 * The nearest *element* in one direction, which is where "element siblings, not
 * node siblings" lives: the DOM stores the newline between two elements as a
 * text node, and this is the walk that steps over it. Text and comments are
 * skipped; the document node ends the walk, because it is not an element and has
 * no element above it.
 */
static CssElementRef CssDomElementInDirection(
    const CssDomHost *dom,
    HtmlNodeHandle node,
    int forward)
{
    size_t steps = 0u;

    while (steps++ < dom->count + 2u) {
        HtmlNodeHandle next;
        HtmlNodeKind kind;
        Result found;

        found = forward ? HtmlNodeNextSibling(&node, &next, NULL)
                        : HtmlNodePrevSibling(&node, &next, NULL);
        if (found != RoboOk) {
            return RoboCssElementRefNone;
        }
        if (HtmlNodeHandleResolve(&next, &kind) != RoboOk) {
            return RoboCssElementRefNone;
        }
        if (kind == RoboHtmlNodeKindElement) {
            return CssDomRefOfHandle(dom, &next);
        }
        node = next;
    }
    return RoboCssElementRefNone;
}

/** The nearest element ancestor of @p node, or NONE at the top of the tree. */
static CssElementRef CssDomAncestorOf(const CssDomHost *dom,
                                                  HtmlNodeHandle node)
{
    size_t steps = 0u;

    while (steps++ < dom->count + 2u) {
        HtmlNodeHandle parent;
        HtmlNodeKind kind;

        if (HtmlNodeParent(&node, &parent, NULL) != RoboOk) {
            return RoboCssElementRefNone;
        }
        if (HtmlNodeHandleResolve(&parent, &kind) != RoboOk) {
            return RoboCssElementRefNone;
        }
        if (kind == RoboHtmlNodeKindElement) {
            return CssDomRefOfHandle(dom, &parent);
        }
        node = parent;
    }
    return RoboCssElementRefNone;
}

static Result CssDomCollectVisitor(const HtmlNodeView *view,
                                             size_t depth,
                                             void *user)
{
    CssDomHost *dom = (CssDomHost *)user;
    CssDomElement *element;
    HtmlAttribute attribute;
    size_t i;
    int truncated = 0;

    (void)depth;
    RoboCheckEqInt(dom->count < dom->capacity, 1);
    element = &dom->elements[dom->count];
    memset(element, 0, sizeof(*element));
    element->handle = view->handle;
    element->tag = CssSliceFromText(view->name ? view->name : "");

    for (i = 0u; HtmlElementAttributeAt(&view->handle, i, &attribute, NULL) == RoboOk;
         i++) {
        if (attribute.nameLength == 2u && attribute.name[0] == 'i' &&
            attribute.name[1] == 'd') {
            element->hasId = 1;
            element->id = CssSliceFromBytes((const uint8_t *)attribute.value,
                                                    attribute.valueLength);
            continue;
        }
        if (attribute.nameLength == 5u && memcmp(attribute.name, "class", 5u) == 0) {
            CssSlice value =
                CssSliceFromBytes((const uint8_t *)attribute.value,
                                          attribute.valueLength);

            element->classCount = CssSplitAsciiWhitespace(
                value, element->classes, CSS_DOM_MAX_CLASSES, &truncated);
            RoboCheckEqInt(truncated, 0);
            continue;
        }
        RoboCheckEqInt(element->attributeCount < CSS_DOM_MAX_ATTRIBUTES, 1);
        element->attributes[element->attributeCount].name =
            CssSliceFromBytes((const uint8_t *)attribute.name,
                                      attribute.nameLength);
        element->attributes[element->attributeCount].value =
            CssSliceFromBytes((const uint8_t *)attribute.value,
                                      attribute.valueLength);
        element->attributeCount++;
    }

    dom->count++;
    return RoboOk;
}

/** Build @p dom over @p html: the real tree builder, then the real DOM links. */
static void CssDomBuild(CssDomHost *dom, const char *html)
{
    HtmlSource source;
    HtmlNodeHandle root;
    size_t i;

    CssFixture();
    memset(dom, 0, sizeof(*dom));
    RoboCheckEqInt(HtmlInit() == RoboOk ||
                          HtmlInit() == RoboAlreadyExists,
                      1);
    RoboCheckEqInt(HtmlDocumentCreate(&dom->document, NULL), RoboOk);
    HtmlSourceInit(&source);
    RoboCheckEqInt(HtmlSourceString(&source, html, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(dom->document, &source, NULL, NULL), RoboOk);

    /* One slot per node is a true upper bound on the element count, and it means
     * the element array never has to move while a view points into it. */
    dom->capacity = HtmlDocumentNodeCount(dom->document) + 1u;
    dom->elements =
        (CssDomElement *)RoboCalloc(dom->capacity, sizeof(CssDomElement));
    RoboCheck(dom->elements != NULL);

    root = HtmlDocumentRoot(dom->document);
    RoboCheckEqInt(HtmlNodeWalkPreOrder(&root, RoboHtmlNodeFilterElements,
                                                    CssDomCollectVisitor, dom, NULL),
                      RoboOk);
    for (i = 0u; i < dom->count; i++) {
        dom->elements[i].parent = CssDomAncestorOf(dom, dom->elements[i].handle);
        dom->elements[i].previous =
            CssDomElementInDirection(dom, dom->elements[i].handle, 0);
        dom->elements[i].next =
            CssDomElementInDirection(dom, dom->elements[i].handle, 1);
    }

    CssElementHostInit(&dom->host);
    dom->host.userdata = dom;
    dom->host.view = CssDomViewOf;
    dom->host.parent = CssDomParentOf;
    dom->host.PreviousSibling = CssDomPreviousOf;
    dom->host.NextSibling = CssDomNextOf;
}

static void CssDomDestroy(CssDomHost *dom)
{
    if (dom->elements) {
        RoboFree(dom->elements);
        dom->elements = NULL;
    }
    if (HtmlDocumentIsLive(dom->document)) {
        RoboCheckEqInt(HtmlDocumentDestroy(dom->document, NULL), RoboOk);
    }
    dom->document = RoboHtmlDocumentIdNone;
    dom->count = 0u;
    dom->capacity = 0u;
}

/* ------------------------------------------------------------------ */
/* Queries                                                            */
/* ------------------------------------------------------------------ */

/** Does @p text match the element @p ref names? Asserts the attempt was decided. */
static int CssDomMatch(CssDomHost *dom, const char *text, CssElementRef ref)
{
    CssSelectorList list = RoboCssCleared;
    CssSelectorMatch result;
    int matched;

    CssSelectParse(text, &list);
    CssSelectorMatchInit(&result);
    matched = CssSelectorListMatches(&list, &dom->host, ref, &result, NULL);
    /* A real DOM can answer every relation the seam defines, so nothing in this
     * suite may come back undecided. */
    RoboCheckEqInt(result.unsupported, 0);
    CssSelectorListDispose(&list);
    return matched;
}

/** How many elements of @p dom @p text matches. */
static size_t CssDomCount(CssDomHost *dom, const char *text)
{
    size_t hits = 0u;
    size_t i;

    for (i = 1u; i <= dom->count; i++) {
        if (CssDomMatch(dom, text, i)) {
            hits++;
        }
    }
    return hits;
}

/**
 * The one element @p text matches — or 0, which happens both when it matched
 * nothing and when it matched more than one. The callers assert non-zero, so a
 * selector that grew an extra match fails the test rather than silently passing
 * on whichever element came first.
 */
static CssElementRef CssDomOnly(CssDomHost *dom, const char *text)
{
    CssElementRef found = RoboCssElementRefNone;
    size_t i;

    for (i = 1u; i <= dom->count; i++) {
        if (CssDomMatch(dom, text, i)) {
            if (found != RoboCssElementRefNone) {
                return RoboCssElementRefNone;
            }
            found = i;
        }
    }
    return found;
}

static CssElementRef CssDomRefById(const CssDomHost *dom, const char *id)
{
    CssSlice needle = CssSliceFromText(id);
    size_t i;

    for (i = 0u; i < dom->count; i++) {
        if (dom->elements[i].hasId &&
            CssSliceEquals(dom->elements[i].id, needle)) {
            return (CssElementRef)(i + 1u);
        }
    }
    return RoboCssElementRefNone;
}

/** The first element with tag @p tag, in document order, or 0. */
static CssElementRef CssDomRefByTag(const CssDomHost *dom,
                                                 const char *tag)
{
    CssSlice needle = CssSliceFromText(tag);
    size_t i;

    for (i = 0u; i < dom->count; i++) {
        if (CssSliceEquals(dom->elements[i].tag, needle)) {
            return (CssElementRef)(i + 1u);
        }
    }
    return RoboCssElementRefNone;
}

/** Assert @p ref names an element with tag @p tag and, when given, id @p id. */
static void CssDomIs(const CssDomHost *dom,
                       CssElementRef ref,
                       const char *tag,
                       const char *id)
{
    char text[64];
    const CssDomElement *element;

    RoboCheckEqInt(ref != RoboCssElementRefNone, 1);
    element = &dom->elements[ref - 1u];
    text[0] = '\0';
    CssSelectText(element->tag, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, tag), 0);
    if (id) {
        RoboCheckEqInt(element->hasId, 1);
        text[0] = '\0';
        CssSelectText(element->id, text, sizeof(text));
        RoboCheckEqInt(strcmp(text, id), 0);
    }
}

/* ------------------------------------------------------------------ */
/* The visual checkpoint's document                                   */
/* ------------------------------------------------------------------ */

static void CheckpointDom(CssDomHost *dom)
{
    CssDomBuild(dom, DomCheckpointHtml());
}

static void TestCssDomCheckpointTree(void)
{
    CssDomHost dom;

    /* The document of record, through the real tree builder — so the element
     * count here is a statement about the checkpoint's own markup rather than
     * about a fixture this suite made up. */
    CheckpointDom(&dom);
    RoboCheckEqInt(dom.count, 29u);
    CssDomIs(&dom, 1u, "html", NULL);
    CssDomIs(&dom, CssDomRefByTag(&dom, "form"), "form", NULL);
    CssDomIs(&dom, CssDomRefById(&dom, "Overview"), "p", "Overview");
    CssDomIs(&dom, CssDomRefById(&dom, "Country"), "select", "Country");
    RoboCheckEqInt(CssDomRefById(&dom, "Missing"), 0u);

    /* Every element is one element of the tree, which is what makes "ref is
     * index + 1" a usable identity in these tests. */
    RoboCheckEqInt(HtmlDocumentNodeCount(dom.document) > dom.count, 1);
    CssDomDestroy(&dom);
}

static void TestCssDomCheckpointDescendantAndChild(void)
{
    CssDomHost dom;

    CheckpointDom(&dom);

    RoboCheckEqInt(CssDomCount(&dom, "html"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "body"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "head"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "html > head"), CssDomRefByTag(&dom, "head"));
    RoboCheckEqInt(CssDomOnly(&dom, "html > body"), CssDomRefByTag(&dom, "body"));
    RoboCheckEqInt(CssDomOnly(&dom, "html > body > h1"),
                      CssDomRefByTag(&dom, "h1"));
    /* A `title` lives in `head`, so `body title` is nothing even though both
     * elements exist. */
    RoboCheckEqInt(CssDomCount(&dom, "body title"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "head title"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "html > title"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "html title"), 1u);

    /* A `div` selector matches nothing in this document, and the `*` in a
     * compound does not turn a near miss into a hit. */
    RoboCheckEqInt(CssDomCount(&dom, "div"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "*"), dom.count);
    RoboCheckEqInt(CssDomCount(&dom, "html *"), dom.count - 1u);
}

static void TestCssDomCheckpointSiblingsIgnoreTextNodes(void)
{
    CssDomHost dom;

    /*
     * The checkpoint is written one element per line, so there is a text node
     * between every pair of sibling elements. Node-sibling counting would find
     * no adjacent pairs here at all, and would also make every element's
     * `:first-child` false except the first line's — which is precisely what
     * these assertions rule out.
     */
    CheckpointDom(&dom);

    RoboCheckEqInt(CssDomCount(&dom, "h1 + p"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "h1 + p"),
                      (CssElementRef)(CssDomRefByTag(&dom, "h1") + 1u));
    RoboCheckEqInt(CssDomCount(&dom, "p + p"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "p + a"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "a + a"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "a + p"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "h1 + a"), 0u);

    /* The form is a flat run of `label`, control, `label`, control, …, which is
     * the shape the field tests below depend on. */
    RoboCheckEqInt(CssDomCount(&dom, "form > label"), 7u);
    RoboCheckEqInt(CssDomCount(&dom, "label + input"), 5u);
    RoboCheckEqInt(CssDomCount(&dom, "label + textarea"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "label + select"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "label ~ input"), 5u);
    RoboCheckEqInt(CssDomCount(&dom, "label ~ button"), 1u);

    /* `label:first-child` is the statement that the newline before the first
     * label is a text node and not a sibling. */
    RoboCheckEqInt(CssDomCount(&dom, "form > label:first-child"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "form > label:first-child"),
                      CssDomRefById(&dom, "DisplayName") - 1u);
    RoboCheckEqInt(CssDomCount(&dom, "form > :last-child"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "form > :last-child"),
                      CssDomRefByTag(&dom, "button"));
    RoboCheckEqInt(CssDomCount(&dom, "body > h1:first-child"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "body > p:nth-child(2)"), 1u);
    /* Two elements are an only child: `title`, whose parent has one child, and
     * `html`, which has no siblings at all — an inclusive-sibling reading that
     * Selectors 4 and every engine share. */
    RoboCheckEqInt(CssDomCount(&dom, ":only-child"), 2u);
    RoboCheckEqInt(CssDomCount(&dom, "title:only-child"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "html:only-child"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "title:only-child"),
                      CssDomRefByTag(&dom, "title"));
    /* Nothing inside `body` is an only child — `title` is in `head`, and every
     * other parent here has several children. */
    RoboCheckEqInt(CssDomCount(&dom, "body :only-child"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "option:first-child"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "option:last-child"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "select > option + option"), 1u);

    CssDomDestroy(&dom);
}

static void TestCssDomCheckpointAttributesAndForms(void)
{
    CssDomHost dom;

    CheckpointDom(&dom);

    RoboCheckEqInt(CssDomCount(&dom, "a[href]"), 2u);
    RoboCheckEqInt(CssDomCount(&dom, "a[href=\"/Settings\"]"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "a[href=\"/Settings\"]"),
                      CssDomRefByTag(&dom, "a"));
    RoboCheckEqInt(CssDomCount(&dom, "a[href^=\"#\"]"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "a[href$=\"Overview\"]"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "a[href*=\"Sett\"]"), 1u);

    /* `required` and `checked` are boolean attributes: present with no value,
     * which is what `[required]` asks about and `[required=""]` does not. */
    RoboCheckEqInt(CssDomCount(&dom, "input[required]"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "input[required]"),
                      CssDomRefById(&dom, "DisplayName"));
    RoboCheckEqInt(CssDomCount(&dom, "input[checked]"), 2u);
    RoboCheckEqInt(CssDomCount(&dom, "option[selected]"), 1u);

    RoboCheckEqInt(CssDomCount(&dom, "input[type=\"text\"]"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "input[type=\"password\"]"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "input[type=\"number\"]"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "input[type=\"checkbox\"]"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "input[type=\"radio\"]"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "input[type=\"submit\"]"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "button[type=\"submit\"]"), 1u);

    /* `[class~=…]` is a word of the class attribute, and the class attribute is
     * the thing the class selector reads — the two must agree. */
    RoboCheckEqInt(CssDomCount(&dom, "label[for]"), 7u);
    RoboCheckEqInt(CssDomCount(&dom, "label[for=\"Notes\"]"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "form[action=\"/Register\"]"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "form[enctype*=\"urlencoded\"]"), 1u);

    /* The label/control pairing of Phase 8.14, expressed as a selector: every
     * control that follows a label in the form. */
    RoboCheckEqInt(CssDomCount(&dom, "form > label + input"), 5u);
    RoboCheckEqInt(CssDomCount(&dom, "form > label + button"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "form button + button"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "body > form + button"), 1u);

    CssDomDestroy(&dom);
}

static void TestCssDomCheckpointStructural(void)
{
    CssDomHost dom;

    CheckpointDom(&dom);

    /* `:root` is the `html` element and nothing else, even though `head` and
     * `body` are equally parentless-looking from a node's point of view. */
    RoboCheckEqInt(CssDomCount(&dom, ":root"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, ":root"), CssDomRefByTag(&dom, "html"));
    RoboCheckEqInt(CssDomCount(&dom, "head:root"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "body :root"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "html:root > body"), 1u);

    RoboCheckEqInt(CssDomCount(&dom, "body > :nth-child(1)"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "body > :nth-child(2)"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "body > :nth-child(3)"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "body > :nth-last-child(1)"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "body > :nth-last-child(1)"),
                      CssDomRefByTag(&dom, "button") + 1u);
    /* body's children are h1, p, p, a, a, p#Overview, form, button: the `p`s
     * sit at 2, 3 and 6, so two of them are even-positioned and one odd. */
    RoboCheckEqInt(CssDomCount(&dom, "body > p:nth-child(odd)"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "body > p:nth-child(even)"), 2u);
    /* The third child is the second bare paragraph — and #Overview, the third
     * `p`, is the sixth child, which is what makes the position the assertion
     * rather than the tag. */
    RoboCheckEqInt(CssDomCount(&dom, "body > p:nth-child(3)"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "body > p:nth-child(3):not(#Overview)"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "body > p:nth-child(6)"),
                      CssDomRefById(&dom, "Overview"));
    RoboCheckEqInt(CssDomCount(&dom, "form > input:nth-child(4)"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "form > input:nth-child(5)"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "select > option:nth-child(2)"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "select > option:nth-child(2)"),
                      CssDomRefByTag(&dom, "option") + 1u);

    RoboCheckEqInt(CssDomCount(&dom, "p:not(#Overview)"), 2u);
    /* Eight element children of `body`, three of which are `p`. */
    RoboCheckEqInt(CssDomCount(&dom, "body > :not(p)"), 5u);
    RoboCheckEqInt(CssDomCount(&dom, "input:not([type=\"text\"])"), 4u);
    RoboCheckEqInt(CssDomCount(&dom, "label:not([for])"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "body > h1, body > form"), 2u);
    RoboCheckEqInt(CssDomCount(&dom, "html > head + body"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "html > body > h1 ~ p"), 3u);
    RoboCheckEqInt(CssDomCount(&dom, "html > body > h1 ~ a"), 2u);
    RoboCheckEqInt(CssDomCount(&dom, "html > body > h1 ~ form"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "html > body > form ~ button"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "html > body > button ~ form"), 0u);

    CssDomDestroy(&dom);
}

static void TestCssDomCheckpointReportsUnevaluatable(void)
{
    CssDomHost dom;
    CssSelectorList list = RoboCssCleared;
    CssSelectorMatch result;

    /* A real DOM answers every relation, so a negative here is a real negative.
     * What it still cannot answer is state — `:hover` — and that must stay
     * reported rather than becoming "does not match". */
    CheckpointDom(&dom);

    CssSelectParse("a:hover", &list);
    RoboCheckEqInt(CssSelectorListMatches(
                          &list, &dom.host, CssDomRefByTag(&dom, "a"), &result, NULL),
                      0);
    RoboCheckEqInt(result.matched, 0);
    RoboCheckEqInt(result.unsupported, 1);
    CssSelectorListDispose(&list);

    CssSelectParse("a::before", &list);
    RoboCheckEqInt(CssSelectorListMatches(
                          &list, &dom.host, CssDomRefByTag(&dom, "a"), &result, NULL),
                      0);
    RoboCheckEqInt(result.unsupported, 0);
    CssSelectorListDispose(&list);

    CssDomDestroy(&dom);
}

static void TestCssDomCheckpointMatchingDoesNotMutate(void)
{
    CssDomHost dom;
    HtmlNodeHandle body;
    uint32_t before = 0u;
    uint32_t after = 0u;
    size_t nodesBefore;
    size_t i;

    CheckpointDom(&dom);
    nodesBefore = HtmlDocumentNodeCount(dom.document);
    body = dom.elements[CssDomRefByTag(&dom, "body") - 1u].handle;
    RoboCheckEqInt(HtmlNodeTreeVersion(dom.document, &before, NULL), RoboOk);

    /* A sweep over the whole document, repeatedly: matching is a read, and the
     * tree must be byte-identical afterwards. */
    for (i = 0u; i < 3u; i++) {
        RoboCheckEqInt(CssDomCount(&dom, "*"), dom.count);
        (void)CssDomCount(&dom, "form > label + input");
        (void)CssDomCount(&dom, "body > :nth-last-child(2)");
        (void)CssDomCount(&dom, "input:not([type]):not(#Country)");
    }

    RoboCheckEqInt(HtmlNodeTreeVersion(dom.document, &after, NULL), RoboOk);
    RoboCheckEqInt(body.nodeId != RoboHtmlNodeIdNone, 1);
    RoboCheckEqInt(before, after);
    RoboCheckEqInt(HtmlDocumentNodeCount(dom.document), nodesBefore);
    RoboCheckEqInt(dom.count, 29u);
    /* … and the tree audit itself agrees the links are still a tree. */
    {
        HtmlTreeReport report;

        RoboCheckEqInt(HtmlDocumentValidateTree(dom.document, &report, NULL),
                          RoboOk);
        RoboCheckEqInt(report.ok, 1);
        RoboCheckEqInt(report.cycles, 0u);
        RoboCheckEqInt(report.badParents, 0u);
        RoboCheckEqInt(report.siblingMismatches, 0u);
    }

    CssDomDestroy(&dom);
}

/* ------------------------------------------------------------------ */
/* Text and comments between elements                                 */
/* ------------------------------------------------------------------ */

static void TestCssDomTextAndCommentsAreNotSiblings(void)
{
    /*
     * The same property as the checkpoint's, made explicit and concentrated:
     * every relation here is separated by non-element nodes, so a host that
     * counted nodes would answer every one of them differently.
     */
    CssDomHost dom;
    HtmlNodeHandle body;
    size_t childNodes = 0u;

    CssDomBuild(&dom,
                  "<html><body>\n"
                  "  <h1>Title</h1>\n"
                  "  <!-- a comment on its own line -->\n"
                  "  <p id=\"one\">One</p>\n"
                  "  \n"
                  "  <p id=\"two\">Two</p>\n"
                  "  <span id=\"three\">Three</span>\n"
                  "</body></html>\n");

    RoboCheckEqInt(dom.count, 7u);
    body = dom.elements[CssDomRefByTag(&dom, "body") - 1u].handle;
    RoboCheckEqInt(HtmlNodeChildCount(&body, &childNodes, NULL), RoboOk);
    /* Eleven nodes for four elements: text runs, a blank line and a comment, all
     * of which the host must look straight through. Five or more of the seven
     * elements would be invisible to a host that counted nodes. */
    RoboCheckEqInt(childNodes, 11u);

    RoboCheckEqInt(CssDomCount(&dom, "h1 + p"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "h1 + p"), CssDomRefById(&dom, "one"));
    RoboCheckEqInt(CssDomCount(&dom, "p + p"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "p + p"), CssDomRefById(&dom, "two"));
    RoboCheckEqInt(CssDomCount(&dom, "p + span"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "h1 + span"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "h1 ~ #three"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "p ~ #three"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "span ~ p"), 0u);

    RoboCheckEqInt(CssDomCount(&dom, "body > :first-child"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "body > :first-child"),
                      CssDomRefByTag(&dom, "h1"));
    RoboCheckEqInt(CssDomCount(&dom, "h1:only-child"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "body > :last-child"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "body > :nth-child(2)"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "body > :nth-child(2)"),
                      CssDomRefById(&dom, "one"));
    RoboCheckEqInt(CssDomCount(&dom, "body > :nth-child(3)"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "body > :nth-child(4)"), 1u);
    RoboCheckEqInt(CssDomCount(&dom, "body > :nth-child(5)"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "body > :only-child"), 0u);
    /* A comment is not an element, so it neither matches `*` here nor counts as
     * a sibling. */
    RoboCheckEqInt(CssDomCount(&dom, "body > *"), 4u);

    CssDomDestroy(&dom);
}

/* ------------------------------------------------------------------ */
/* Deep trees                                                         */
/* ------------------------------------------------------------------ */

static void TestCssDomDeepTree(void)
{
    /*
     * A reasonably deep document — deeper than any recursion budget that would
     * be comfortable, and exactly the shape that turns a bounded walk into a
     * stack overflow if the implementation recursed per tree level rather than
     * per compound.
     *
     * Every div carries `id="dN"` so an assertion can name the level it landed
     * on, and the innermost element is the only `<em>` in the document.
     */
    static char html[CSS_DOM_DEEP_LEVELS * 32u + 128u];
    CssDomHost dom;
    size_t at = 0u;
    size_t level;

    memcpy(html + at, "<html><body>", 12u);
    at += 12u;
    for (level = 1u; level <= CSS_DOM_DEEP_LEVELS; level++) {
        at += CssDomWriteDiv(html + at, sizeof(html) - at, (unsigned)level);
    }
    memcpy(html + at, "<em id=\"leaf\">deep</em>", 23u);
    at += 23u;
    for (level = 0u; level < CSS_DOM_DEEP_LEVELS; level++) {
        memcpy(html + at, "</div>", 6u);
        at += 6u;
    }
    memcpy(html + at, "</body></html>", 14u);
    at += 14u;
    html[at] = '\0';
    RoboCheckEqInt(at < sizeof(html), 1);

    CssDomBuild(&dom, html);
    /* `html`, an implied `head`, `body`, every level, and the leaf. */
    RoboCheckEqInt(dom.count, CSS_DOM_DEEP_LEVELS + 4u);

    /* The deepest element is reachable from the root, and from every level in
     * between — which is the descendant walk climbing the whole chain. */
    RoboCheckEqInt(CssDomCount(&dom, "#leaf"), 1u);
    RoboCheckEqInt(CssDomOnly(&dom, "html em"), CssDomRefById(&dom, "leaf"));
    RoboCheckEqInt(CssDomOnly(&dom, "body em"), CssDomRefById(&dom, "leaf"));
    RoboCheckEqInt(CssDomOnly(&dom, "div em"), CssDomRefById(&dom, "leaf"));
    RoboCheckEqInt(CssDomOnly(&dom, "em"), CssDomRefById(&dom, "leaf"));
    /* A long complex selector, matched from the right-most compound down the
     * whole chain: the compound recursion is what bounds the stack, not the
     * tree. */
    RoboCheckEqInt(CssDomOnly(&dom, "html body div div div div div div div em"),
                      CssDomRefById(&dom, "leaf"));
    RoboCheckEqInt(CssDomCount(&dom, "html body div div div div div div div span"),
                      0u);

    /* Every level is the first and last child of its parent, so every level
     * answers the structural pseudo-classes identically. */
    for (level = 1u; level <= CSS_DOM_DEEP_LEVELS; level++) {
        char id[16];
        char selector[64];

        CssDomWriteId(id, sizeof(id), (unsigned)level);
        CssDomJoin(selector, sizeof(selector), "#", id, ":first-child");
        RoboCheckEqInt(CssDomCount(&dom, selector), 1u);
        CssDomJoin(selector, sizeof(selector), "#", id, ":last-child");
        RoboCheckEqInt(CssDomCount(&dom, selector), 1u);
        CssDomJoin(selector, sizeof(selector), "#", id, ":only-child");
        RoboCheckEqInt(CssDomCount(&dom, selector), 1u);
        CssDomJoin(selector, sizeof(selector), "#", id, ":nth-child(1)");
        RoboCheckEqInt(CssDomCount(&dom, selector), 1u);
        CssDomJoin(selector, sizeof(selector), "#", id, ":nth-child(2)");
        RoboCheckEqInt(CssDomCount(&dom, selector), 0u);
        CssDomJoin(selector, sizeof(selector), "#", id, ":nth-last-child(1)");
        RoboCheckEqInt(CssDomCount(&dom, selector), 1u);
        CssDomJoin(selector, sizeof(selector), "div#", id, ":root");
        RoboCheckEqInt(CssDomCount(&dom, selector), 0u);
    }

    /* Each level's parent is the level above it, expressed as a child selector
     * and checked against its own id — a chain of `>` that only a correct
     * parent link can satisfy. */
    for (level = 2u; level <= CSS_DOM_DEEP_LEVELS; level++) {
        char parentId[16];
        char childId[16];
        char selector[64];

        CssDomWriteId(parentId, sizeof(parentId), (unsigned)(level - 1u));
        CssDomWriteId(childId, sizeof(childId), (unsigned)level);
        strcpy(selector, "#");
        strcat(selector, parentId);
        strcat(selector, " > #");
        strcat(selector, childId);
        RoboCheckEqInt(strlen(selector) < sizeof(selector), 1);
        RoboCheckEqInt(CssDomCount(&dom, selector), 1u);
    }

    CssDomDestroy(&dom);
}

/* ------------------------------------------------------------------ */
/* Mutation                                                           */
/* ------------------------------------------------------------------ */

static void TestCssDomRebuildAfterMutation(void)
{
    /*
     * The host resolves its relations once, from the DOM's own links, so a
     * structural change makes them stale and the fix is to build a new host. This
     * test does exactly that and asserts the *new* shape, which is what makes the
     * requirement a contract rather than a footnote: relations that were cached
     * as values could not be refreshed at all.
     */
    CssDomHost dom;
    HtmlNodeHandle body;
    HtmlNodeHandle second;
    uint32_t versionBefore = 0u;
    uint32_t versionAfter = 0u;

    CssDomBuild(&dom,
                  "<html><body><h1>T</h1><p id=\"one\">One</p>"
                  "<p id=\"two\">Two</p></body></html>");
    /* `html`, an implied `head`, `body`, `h1`, `p#one` and `p#two`. */
    RoboCheckEqInt(dom.count, 6u);
    RoboCheckEqInt(CssDomOnly(&dom, "#two:last-child"),
                      CssDomRefById(&dom, "two"));
    RoboCheckEqInt(CssDomOnly(&dom, "#one + p"), CssDomRefById(&dom, "two"));
    RoboCheckEqInt(CssDomCount(&dom, "body > :nth-child(3)"), 1u);

    body = dom.elements[CssDomRefByTag(&dom, "body") - 1u].handle;
    second = dom.elements[CssDomRefById(&dom, "two") - 1u].handle;
    RoboCheckEqInt(HtmlNodeTreeVersion(dom.document, &versionBefore, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&body, &second, NULL), RoboOk);
    /* The mutation moved the tree version — which is the fact that says the
     * host's resolved relations are now stale, and the reason it is rebuilt. */
    RoboCheckEqInt(HtmlNodeTreeVersion(dom.document, &versionAfter, NULL),
                      RoboOk);
    RoboCheckEqInt(versionAfter != versionBefore, 1);

    CssDomDestroy(&dom);
    CssDomBuild(&dom,
                  "<html><body><h1>T</h1><p id=\"one\">One</p>"
                  "<p id=\"two\">Two</p></body></html>");
    /* Rebuilt from the unmutated source: the same answers, which shows the
     * teardown leaves nothing behind. */
    RoboCheckEqInt(CssDomOnly(&dom, "#one + p"), CssDomRefById(&dom, "two"));
    CssDomDestroy(&dom);

    /* And now the mutated document itself, rebuilt from scratch. */
    CssDomBuild(&dom, "<html><body><h1>T</h1><p id=\"one\">One</p></body></html>");
    RoboCheckEqInt(CssDomOnly(&dom, "#one:last-child"),
                      CssDomRefById(&dom, "one"));
    RoboCheckEqInt(CssDomCount(&dom, "#one + p"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "body > :nth-child(3)"), 0u);
    RoboCheckEqInt(CssDomCount(&dom, "body > :nth-child(2)"), 1u);

    CssDomDestroy(&dom);
}

/* ------------------------------------------------------------------ */
/* The visual vertical slice                                         */
/* ------------------------------------------------------------------ */

/** An RGBA8 surface the painter can write and the test can read. */
/*
 * The three `*_does_not_disturb_the_picture` cases below compare a **frame** before and after the work
 * they are about. Until this phase they compared the deleted demo layer's own painting of the same
 * document; now they compare the production pipeline — markup → style → layout → records → display list →
 * rasteriser — through the render fixture, which is the chain a window shows. The claim is therefore
 * stronger than it was: a change that reached a screen without reaching this comparison is no longer
 * possible, because there is only one picture path left.
 *
 * The two frames are rendered from the document *text* rather than from the suite's own DOM, which is the
 * one half of the original claim that has moved. It is asserted directly instead, and on the same tree: a
 * fingerprint of the DOM (its element count, and every element's tag and id, read through the suite's own
 * adapter) is taken before and after the work runs, so a matcher or a cascade pass that mutated the tree
 * fails there even though the frame comparison no longer reads that tree.
 */
static const char *CSS_DOM_CHECKPOINT_CSS =
    "html, body { display: block; margin: 0; padding: 0; background-color: #ffffff }\n"
    "h1, p, form, label, a, button { display: block; color: #1d1d1f }\n"
    "form { background-color: #f5f5f7; padding: 8px }\n"
    "button { background-color: #0e7aff; color: #ffffff; padding: 4px }\n";

/** The checkpoint document's own frame, through the production pipeline. */
static RenderTarget *CssDomPicture(void)
{
    GfxColorRgba8 page = { 255u, 255u, 255u, 255u };
    RenderTarget *target;
    RenderScene scene;

    RenderSceneBegin(&scene, DomCheckpointHtml(), CSS_DOM_CHECKPOINT_CSS);
    target = RenderPage(900u, 800u, page);
    RoboCheckEqInt(RenderRasterize(scene.display, target, NULL, NULL, NULL), RoboOk);
    /* A frame with nothing in it would make every comparison below pass for the wrong reason. */
    RenderRegionHasInkIs(target, 0, 0, 900, 800, page,
                             "the checkpoint document paints ink");
    RenderSceneEnd(&scene);
    return target;
}

/** FNV-1a over the DOM's shape: the element count, and every element's tag and id. */
static uint64_t CssDomFingerprintText(uint64_t hash, const char *text)
{
    size_t index;

    for (index = 0u; text[index] != '\0'; index++) {
        hash ^= (uint64_t)(unsigned char)text[index];
        hash *= 1099511628211ull;
    }
    /* A separator, so a tag ending where an id begins still hashes differently from the concatenation. */
    hash ^= 0x2Fu;
    hash *= 1099511628211ull;
    return hash;
}

static uint64_t CssDomFingerprint(const CssDomHost *dom)
{
    uint64_t hash = 1469598103934665603ull; /* FNV-1a offset basis */
    char text[64];
    CssElementRef ref;

    hash ^= (uint64_t)dom->count;
    hash *= 1099511628211ull;
    for (ref = 1u; ref <= dom->count; ref++) {
        const CssDomElement *element = &dom->elements[ref - 1u];

        text[0] = '\0';
        CssSelectText(element->tag, text, sizeof(text));
        hash = CssDomFingerprintText(hash, text);
        text[0] = '\0';
        if (element->hasId) {
            CssSelectText(element->id, text, sizeof(text));
        }
        hash = CssDomFingerprintText(hash, text);
    }
    return hash;
}

static void TestCssDomSelectorsDoNotDisturbThePicture(void)
{
    /*
     * The vertical slice, end to end, with selector matching inserted in the
     * middle of it: the checkpoint document is painted, matched against with the
     * whole selector repertoire, and painted again. The two frames must be
     * byte-identical, and the tree the repertoire ran over must be untouched.
     *
     * Both halves are asserted, and both are needed: a matcher that left a
     * borrowed slice dangling long enough to corrupt the payload arena fails the
     * frame comparison, and a matcher that mutated an attribute, reordered a
     * child or dropped an element fails the fingerprint — which is the half the
     * frame comparison would no longer see, because the frames are rendered from
     * the document's text (see the note above `CssDomPicture`).
     */
    static const char *const repertoire[] = {
        "*", "html", "body > *", ":root", ":first-child", ":last-child",
        ":only-child", ":nth-child(2n+1)", ":nth-last-child(odd)",
        "label + input", "label ~ input", "h1 + p", "p + p", "a + a",
        "form > label", "form > :last-child", "select > option:last-child",
        "input[required]", "input[checked]", "option[selected]",
        "input[type=\"checkbox\"]", "a[href^=\"#\"]", "a[href$=\"Settings\"]",
        "body > h1 ~ p", "html > body > form ~ button", "#Overview",
        ":not(p)", "body > :not(form)", "p:not(#Overview)",
        "input:not([type=\"text\"])", ":not(:not(.missing))",
        "form > label:first-child", "form > label:nth-child(3)"
    };
    CssDomHost dom;
    RenderTarget *before = NULL;
    RenderTarget *after = NULL;
    uint64_t fingerprintBefore;
    size_t i;

    CssDomBuild(&dom, DomCheckpointHtml());
    RoboCheckEqInt(dom.count, 29u);
    fingerprintBefore = CssDomFingerprint(&dom);
    before = CssDomPicture();

    /* The whole repertoire, against every element, twice over. */
    for (i = 0u; i < sizeof(repertoire) / sizeof(repertoire[0]); i++) {
        CssElementRef ref;

        for (ref = 1u; ref <= dom.count; ref++) {
            (void)CssDomMatch(&dom, repertoire[i], ref);
        }
    }
    /* And a malformed selector in the same breath: a rejected one must leave the
     * document no worse off than a matched one. */
    (void)CssSelectParseRejected("div >");
    (void)CssSelectParseRejected("[attr=");
    (void)CssSelectParseRejected(":nth-child(");

    /* The tree the repertoire ran over, unchanged. */
    RoboCheckEqInt((int)CssDomFingerprint(&dom), (int)fingerprintBefore);
    RoboCheckEqInt(dom.count, 29u);

    after = CssDomPicture();
    RenderTargetsEqual(before, after, "selector matching changed the rendered frame");

    RenderTargetEnd(before);
    RenderTargetEnd(after);
    CssDomDestroy(&dom);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                        */
/* ------------------------------------------------------------------ */

static void TestCssDomMatchingIsDeterministic(void)
{
    /* Two hosts built independently over the same source must agree on every
     * selector, element for element. Matching that depended on allocation order,
     * address values or walk history would show up here. */
    static const char *const selectors[] = {
        "*", "html", "body > *", "form > *", "label + input", "p:nth-child(2)",
        "input[type=\"text\"]", "a[href^=\"#\"]", "#Overview", ":root",
        "form > label:not(:first-child)", "body > :nth-last-child(2)",
        "html > body > h1 ~ p", "select option:last-child"
    };
    CssDomHost first;
    CssDomHost second;
    size_t i;

    CheckpointDom(&first);
    CheckpointDom(&second);
    RoboCheckEqInt(first.count, second.count);
    for (i = 0u; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        CssElementRef ref;

        RoboCheckEqInt(CssDomCount(&first, selectors[i]),
                          CssDomCount(&second, selectors[i]));
        for (ref = 1u; ref <= first.count; ref++) {
            RoboCheckEqInt(CssDomMatch(&first, selectors[i], ref),
                              CssDomMatch(&second, selectors[i], ref));
        }
    }
    CssDomDestroy(&first);
    CssDomDestroy(&second);
}

/* ------------------------------------------------------------------ */
/* The cascade over the real DOM (Phase 9.5)                           */
/* ------------------------------------------------------------------ */

/*
 * The matching suites already prove the matcher against the real tree, so what
 * is left for the cascade is the two things only a real document has.
 *
 * The first is the element's own `style` attribute. It lives in the DOM and
 * nowhere else: RoboCSS asks for its *text* through the seam, parses it, and
 * keeps none of it, so the only way to prove the boundary works is to hand the
 * callback a real document and check that what comes back is what the attribute
 * says. The stub cannot test that, because the stub is the thing under test.
 *
 * The second is an ordering produced by a tree the caller did not shape. A
 * descendant combinator here has to reach through ancestors the host resolves
 * from real parent links, and a sibling through the text nodes the tree builder
 * really inserted — so a cascade that got its relations from a convenient copy
 * of the document would still collect the wrong set of rules.
 */

/** The DOM's own `style` attribute, which is what the inline boundary hands over. */
static Result CssDomInlineStyle(void *userdata,
                                         CssElementRef ref,
                                         CssSlice *outText)
{
    CssDomHost *dom = (CssDomHost *)userdata;
    HtmlAttribute attribute;
    size_t index;

    if (!dom || ref == RoboCssElementRefNone || ref > dom->count) {
        return RoboNotFound;
    }
    for (index = 0u;
         HtmlElementAttributeAt(&dom->elements[ref - 1u].handle, index,
                                        &attribute, NULL) == RoboOk;
         index++) {
        if (attribute.nameLength == 5u && memcmp(attribute.name, "style", 5u) == 0) {
            *outText = CssSliceFromBytes((const uint8_t *)attribute.value,
                                                  attribute.valueLength);
            return RoboOk;
        }
    }
    /* No `style` attribute is a *different* answer from an empty one, and the
     * DOM is where that distinction is real rather than theoretical. */
    return RoboNotFound;
}

typedef struct cssDomCascade {
    CssDomHost dom;
    CssStyleContext *context;
    /* 9.8's two halves, so the fixture is a complete host: identity for property
     * names, and CSS's own facts about them (does it inherit by default, what is
     * its initial value). A context without them can match and resolve; only a
     * context with them can *compute*. */
    CssPropertyTable *table;
    CssPropertyRegistry *registry;
} CssDomCascade;

/** Build @p html, then a context over the real document and its own attributes. */
static void CssDomCascadeBegin(CssDomCascade *fixture, const char *html)
{
    CssDomBuild(&fixture->dom, html);
    fixture->context = NULL;
    fixture->table = NULL;
    fixture->registry = NULL;
    RoboCheckEqInt(CssStyleContextCreate(&fixture->context, NULL), RoboOk);
    RoboCheckEqInt(CssStyleContextSetElementHost(fixture->context,
                                                             &fixture->dom.host, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleContextSetInlineStyle(
                          fixture->context, CssDomInlineStyle, &fixture->dom, NULL),
                      RoboOk);
    RoboCheckEqInt(CssPropertyTableCreate(&fixture->table, NULL), RoboOk);
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&fixture->registry, NULL),
                      RoboOk);
    RoboCheckEqInt(CssPropertyRegistryApplyToTable(fixture->registry,
                                                               fixture->table, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleContextSetPropertyTable(fixture->context,
                                                               fixture->table, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleContextSetPropertyRegistry(fixture->context,
                                                                  fixture->registry, NULL),
                      RoboOk);
}

static void CssDomCascadeEnd(CssDomCascade *fixture)
{
    CssStyleContextDestroy(fixture->context);
    fixture->context = NULL;
    CssPropertyRegistryDestroy(fixture->registry);
    fixture->registry = NULL;
    CssPropertyTableDestroy(fixture->table);
    fixture->table = NULL;
    CssDomDestroy(&fixture->dom);
}

/** Parse @p css as an author sheet, parse its rules' selectors, add it. */
static CssStylesheetId CssDomCascadeSheet(CssDomCascade *fixture,
                                                      const char *css)
{
    CssStylesheetId id = CssParseSheet(css, NULL);
    size_t index;

    for (index = 0u; index < CssStylesheetRuleCount(id); index++) {
        CssStyleRule *rule = (CssStyleRule *)CssRuleStyle(
            CssStylesheetRule(id, index));

        if (rule) {
            CssError error = RoboCssCleared;

            RoboCheckEqInt(CssStyleRuleParseSelectors(
                                  rule, CssStylesheetSource(id), &error, NULL),
                              RoboOk);
            RoboCheckEqInt(CssErrorIsError(&error), 0);
        }
    }
    RoboCheckEqInt(CssStyleContextAddSheet(fixture->context,
                                                      RoboCssOriginAuthor, id, NULL),
                      RoboOk);
    return id;
}

static void CssDomCascadeCollect(CssDomCascade *fixture,
                                    CssElementRef ref,
                                    CssCascade *out)
{
    CssError error = RoboCssCleared;

    RoboCheckEqInt(CssStyleContextMatchDeclarations(
                          fixture->context, ref, out, &error, NULL),
                      RoboOk);
}

/** Compute @p ref's style through the chain door, asserting the call succeeded. */
static void CssDomCompute(CssDomCascade *fixture,
                            CssElementRef ref,
                            CssComputedStyle **outStyle)
{
    CssError error = RoboCssCleared;

    RoboCheckEqInt(CssComputedStyleCreate(outStyle, NULL), RoboOk);
    RoboCheckEqInt(CssStyleContextComputeStyle(fixture->context, ref, *outStyle,
                                                          &error, NULL),
                      RoboOk);
    RoboCheckEqInt(CssErrorIsError(&error), 0);
}

/** The winning value's first token, as a NUL-terminated string. */
static void CssDomCascadeWinner(const CssCascade *cascade,
                                   const char *name,
                                   char *out,
                                   size_t capacity)
{
    const CssCascadeCandidate *winner = NULL;
    const CssValue *value;

    if (out && capacity > 0u) {
        out[0] = '\0';
    }
    RoboCheckEqInt(CssCascadeResolveName(
                          cascade, CssSliceFromText(name), &winner),
                      RoboCssCascadeVerdictDecided);
    if (!winner) {
        return;
    }
    value = CssDeclarationValue(winner->declaration);
    if (value && value->kind == RoboCssValueKindList && value->children.count > 0u) {
        value = &value->children.items[0];
    }
    if (value && value->kind == RoboCssValueKindToken) {
        CssSelectText(value->token.text, out, capacity);
    }
}

static void TestCssDomCascadeInlineAttributeComesFromTheDom(void)
{
    static const char *const html =
        "<div id=\"Card\" style=\"color: inline-style\"><p id=\"Text\">hello</p></div>";
    CssDomCascade fixture;
    CssCascade cascade = RoboCssCleared;
    CssStylesheetId sheet;
    CssElementRef card;
    CssElementRef text;
    char value[32];

    CssDomCascadeBegin(&fixture, html);
    sheet = CssDomCascadeSheet(&fixture, "#Card { color: from-sheet; padding: 8px }");
    card = CssDomRefById(&fixture.dom, "Card");
    text = CssDomRefById(&fixture.dom, "Text");
    RoboCheckEqInt(card != RoboCssElementRefNone, 1);

    CssDomCascadeCollect(&fixture, card, &cascade);
    /* The attribute was found, handed over as text, and parsed by RoboCSS — the
     * DOM still owns the bytes it came from. */
    RoboCheckEqInt(cascade.inlineChecked, 1);
    RoboCheckEqInt(cascade.inlinePresent, 1);
    RoboCheckEqInt(cascade.hasInlineError, 0);
    RoboCheckEqInt(cascade.unsupported, 0);
    /* The sheet's `color` and `padding`, then the attribute's `color` — which
     * is where it belongs, last, because inline is the strongest normal level. */
    RoboCheckEqInt(CssCascadeCount(&cascade), 3u);
    CssDomCascadeWinner(&cascade, "color", value, sizeof(value));
    RoboCheckStrEq(value, "inline-style");
    /* The sheet's uncontested property is still decided, so the inline level did
     * not swallow the whole cascade. */
    CssDomCascadeWinner(&cascade, "padding", value, sizeof(value));
    RoboCheckStrEq(value, "8px");
    /* The inline candidate really is the inline one, not a lucky author rule. */
    RoboCheckEqInt(cascade.items[2].origin, RoboCssOriginInline);
    RoboCheckEqInt(cascade.items[2].level, RoboCssCascadeLevelInline);
    RoboCheckEqInt(cascade.items[2].declarationIndex, 0u);
    RoboCheckEqInt(cascade.items[2].sheet, RoboCssStylesheetIdNone);
    CssCascadeDispose(&cascade);

    /* The nested element has no `style` attribute at all. */
    CssDomCascadeCollect(&fixture, text, &cascade);
    RoboCheckEqInt(cascade.inlineChecked, 1);
    RoboCheckEqInt(cascade.inlinePresent, 0);
    RoboCheckEqInt(cascade.hasInlineError, 0);
    RoboCheckEqInt(CssCascadeCount(&cascade), 0u);
    CssCascadeDispose(&cascade);

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssDomCascadeEnd(&fixture);
}

static void TestCssDomCascadeUsesTheRealTreeRelations(void)
{
    static const char *const html =
        "<section id=\"Outer\"><div class=\"inner\"><p id=\"Deep\">x</p></div></section>";
    CssDomCascade fixture;
    CssCascade cascade = RoboCssCleared;
    CssStylesheetId sheet;
    CssElementRef deep;
    char value[32];

    CssDomCascadeBegin(&fixture, html);
    /* Three rules that match the same element by different routes: its id, a
     * descendant chain, and a child chain that must *not* match. Which two
     * arrive is a statement about the relations the cascade read from the DOM. */
    sheet = CssDomCascadeSheet(&fixture,
                                  "#Deep { color: by-id }\n"
                                  ".inner p { color: by-descendant }\n"
                                  "#Outer > p { color: by-child }");
    deep = CssDomRefById(&fixture.dom, "Deep");
    RoboCheckEqInt(deep != RoboCssElementRefNone, 1);

    CssDomCascadeCollect(&fixture, deep, &cascade);
    RoboCheckEqInt(cascade.rulesScanned, 3u);
    RoboCheckEqInt(cascade.rulesMatched, 2u);
    RoboCheckEqInt(cascade.rulesUndecided, 0u);
    RoboCheckEqInt(cascade.unsupported, 0);
    RoboCheckEqInt(CssCascadeCount(&cascade), 2u);
    /* Two matched rules, one level: specificity decides, and it is the id rule —
     * (1,0,0) against the descendant chain's (0,1,1). The relations came from
     * the real tree, so the specificity is deciding between two selectors that
     * really did match the real element. */
    CssSpecificityIs(cascade.items[0].specificity, 1u, 0u, 0u);
    CssSpecificityIs(cascade.items[1].specificity, 0u, 1u, 1u);
    CssDomCascadeWinner(&cascade, "color", value, sizeof(value));
    RoboCheckStrEq(value, "by-id");
    /* The candidates carry the sheet and rule they came from, which is how a
     * caller reads back that the child rule was the one left out. */
    RoboCheckEqInt(cascade.items[0].ruleIndex, 0u);
    RoboCheckEqInt(cascade.items[1].ruleIndex, 1u);
    RoboCheckEqInt(cascade.items[1].selectorIndex, 0u);

    CssCascadeDispose(&cascade);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssDomCascadeEnd(&fixture);
}

static void TestCssDomCascadeImportanceOutranksInline(void)
{
    static const char *const html = "<p id=\"Note\" style=\"color: inline\">x</p>";
    CssDomCascade fixture;
    CssCascade cascade = RoboCssCleared;
    CssStylesheetId sheet;
    CssElementRef note;
    char value[32];

    CssDomCascadeBegin(&fixture, html);
    /* An author `!important` beats the element's own normal inline declaration,
     * and the inline one still beats the plain author rule. This is the one
     * ordering that needs the whole table rather than a single comparison, and a
     * real `style` attribute is what makes it a statement about CSS rather than
     * about a stub. */
    sheet = CssDomCascadeSheet(&fixture,
                                  "p { color: plain }\n#Note { color: strong !important }");
    note = CssDomRefById(&fixture.dom, "Note");
    RoboCheckEqInt(note != RoboCssElementRefNone, 1);

    CssDomCascadeCollect(&fixture, note, &cascade);
    RoboCheckEqInt(CssCascadeCount(&cascade), 3u);
    RoboCheckEqInt(cascade.items[0].origin, RoboCssOriginAuthor);
    RoboCheckEqInt(cascade.items[1].origin, RoboCssOriginInline);
    RoboCheckEqInt(cascade.items[2].level,
                      RoboCssCascadeLevelAuthorImportant);
    CssDomCascadeWinner(&cascade, "color", value, sizeof(value));
    RoboCheckStrEq(value, "strong");
    CssCascadeDispose(&cascade);

    /* Without the `!important`, the same three declarations resolve to the
     * inline one — so the assertion above is about importance and not about the
     * order the declarations happened to arrive in. */
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssStyleContextClear(fixture.context);
    sheet = CssDomCascadeSheet(&fixture, "p { color: plain }\n#Note { color: weak }");
    CssDomCascadeCollect(&fixture, note, &cascade);
    RoboCheckEqInt(CssCascadeCount(&cascade), 3u);
    CssDomCascadeWinner(&cascade, "color", value, sizeof(value));
    RoboCheckStrEq(value, "inline");

    CssCascadeDispose(&cascade);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssDomCascadeEnd(&fixture);
}

static void TestCssDomCascadeDoesNotDisturbThePicture(void)
{
    /*
     * Collecting a cascade for every element of the checkpoint document must
     * leave the document exactly as it found it — including its `style`
     * attributes, which this is the first phase to read the *text* of. Both
     * halves of that are asserted: the frame the production pipeline paints
     * before and after, and the fingerprint of the tree itself (see the note
     * above `CssDomPicture`).
     */
    CssDomCascade fixture;
    RenderTarget *before = NULL;
    RenderTarget *after = NULL;
    CssStylesheetId sheet;
    uint64_t fingerprintBefore;
    CssElementRef ref;
    size_t collected = 0u;

    CssDomCascadeBegin(&fixture, DomCheckpointHtml());
    RoboCheckEqInt(fixture.dom.count, 29u);
    sheet = CssDomCascadeSheet(&fixture,
                                  "* { color: red }\nhtml body > * { padding: 1px }\n"
                                  "form > label + input { color: blue }");
    fingerprintBefore = CssDomFingerprint(&fixture.dom);
    before = CssDomPicture();

    for (ref = 1u; ref <= fixture.dom.count; ref++) {
        CssCascade cascade = RoboCssCleared;

        CssDomCascadeCollect(&fixture, ref, &cascade);
        /* Every element is reached and the document can answer every relation,
         * so no element may come back with an undecided rule. */
        RoboCheckEqInt(cascade.unsupported, 0);
        RoboCheckEqInt(cascade.rulesUndecided, 0u);
        if (CssCascadeCount(&cascade) > 0u) {
            collected++;
        }
        CssCascadeDispose(&cascade);
    }
    RoboCheckEqInt(collected, 29u);

    RoboCheckEqInt((int)CssDomFingerprint(&fixture.dom), (int)fingerprintBefore);
    after = CssDomPicture();
    RenderTargetsEqual(before, after, "collecting a cascade changed the rendered frame");

    RenderTargetEnd(before);
    RenderTargetEnd(after);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssDomCascadeEnd(&fixture);
}

static void TestCssDomSpecificityPicksTheWinner(void)
{
    /* The specification's own example, against the real tree: `p` (0,0,1),
     * `.title` (0,1,0) and `#App .title` (1,1,0) all match the same paragraph,
     * and the cascade must pick the third. Tokenize → parse → match → specificity
     * → cascade, over a real RoboHTML document rather than a host double. */
    static const char *const html =
        "<div id=\"App\"><p id=\"Title\" class=\"title\">Hello</p></div>";
    CssDomCascade fixture;
    CssCascade cascade = RoboCssCleared;
    CssStylesheetId sheet;
    CssElementRef paragraph;
    char value[32];

    CssDomCascadeBegin(&fixture, html);
    sheet = CssDomCascadeSheet(&fixture,
                                  "p { color: from-type }\n"
                                  ".title { color: from-class }\n"
                                  "#App .title { color: from-both }");
    paragraph = CssDomRefById(&fixture.dom, "Title");
    RoboCheckEqInt(paragraph != RoboCssElementRefNone, 1);

    CssDomCascadeCollect(&fixture, paragraph, &cascade);
    RoboCheckEqInt(cascade.rulesMatched, 3u);
    RoboCheckEqInt(cascade.unsupported, 0);
    RoboCheckEqInt(CssCascadeCount(&cascade), 3u);
    /* Ascending specificity in document order, and the last one wins. */
    CssSpecificityIs(cascade.items[0].specificity, 0u, 0u, 1u);
    CssSpecificityIs(cascade.items[1].specificity, 0u, 1u, 0u);
    CssSpecificityIs(cascade.items[2].specificity, 1u, 1u, 0u);
    CssDomCascadeWinner(&cascade, "color", value, sizeof(value));
    RoboCheckStrEq(value, "from-both");

    CssCascadeDispose(&cascade);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssDomCascadeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The whole pipeline, over the visual checkpoint's document (9.8)     */
/* ------------------------------------------------------------------ */

/**
 * `css.dom.computed.pipeline` — tokenize → parse → match → specificity →
 * cascade → inherit → compute, over the document the visual checkpoint paints.
 *
 * The CSS-side suite drives the same stages over a host test double, where an
 * element, its parent and its siblings are whatever the fixture says. This one
 * runs them over the real tree: the parent links come from RoboHTML, the
 * `style` attribute comes from the DOM's own attribute storage, the rules match
 * through real selectors, and the values that come out are the ones the
 * checkpoint document's styles really produce. A stage that only worked against
 * a hand-shaped fixture would pass that suite and fail here.
 *
 * Four things are worth proving, and all four need a *typed* value to say:
 * two selectors of different specificity produce different colours, a child
 * inherits its parent's **computed** length rather than a second reading of what
 * the parent specified, a value that means nothing for its property is reported
 * rather than dropped, and a custom property travels the whole path untouched.
 */
static void TestCssDomComputedPipeline(void)
{
    CssDomCascade fixture;
    CssComputedStyle *overview = NULL;
    CssComputedStyle *label = NULL;
    CssComputedStyle *field = NULL;
    CssComputedStyle *button = NULL;
    CssElementRef paragraph;
    const CssComputedValue *value;
    CssStylesheetId sheet;
    CssColor color;

    CssDomCascadeBegin(&fixture, DomCheckpointHtml());
    sheet = CssDomCascadeSheet(&fixture,
                                  "body { color: green; font-size: 20px; --theme: #0E7AFF }\n"
                                  "p { color: blue }\n"
                                  "#Overview { color: red }\n"
                                  "form { width: 300px }\n"
                                  "label { font-size: inherit; width: inherit; display: inline }\n"
                                  "input { width: 12px }\n"
                                  "button { color: nonsense }");

    /* The document really has the elements every assertion below is about, and
     * `#Overview` is the paragraph the id selector is aimed at. */
    RoboCheck(CssDomRefById(&fixture.dom, "Overview") != RoboCssElementRefNone);
    RoboCheck(CssDomRefByTag(&fixture.dom, "label") != RoboCssElementRefNone);

    /* Specificity decided which declaration applies, and the value that came out
     * is a colour: four channels, not the three letters `red`. */
    CssDomCompute(&fixture, CssDomRefById(&fixture.dom, "Overview"), &overview);
    CssComputedIs(overview, "color", "#ff0000");
    CssComputedFlagsAre(overview, "color", "declared");
    value = CssComputedStyleComputedByName(overview,
                                                     CssSliceFromText("color"));
    RoboCheck(value != NULL);
    RoboCheckEqInt(value ? (int)value->kind : -1, RoboCssComputedValueColor);
    color = CssComputedValueColor(value);
    RoboCheckEqInt(color.red, 255u);
    RoboCheckEqInt(color.green, 0u);
    RoboCheckEqInt(color.blue, 0u);
    RoboCheckEqInt(color.alpha, 255u);

    /* The other paragraph has no id, so the type selector beats the inherited
     * body colour — and the element that carries it is a different one. */
    paragraph = CssDomRefByTag(&fixture.dom, "p");
    RoboCheck(paragraph != CssDomRefById(&fixture.dom, "Overview"));
    {
        CssComputedStyle *other = NULL;

        CssDomCompute(&fixture, paragraph, &other);
        CssComputedIs(other, "color", "#0000ff");
        CssComputedFlagsAre(other, "color", "declared");
        CssComputedStyleDestroy(other);
    }

    /* An inherited value is the parent's *computed* value: `font-size: inherit`
     * on the label lands on the 20px the body computed, as a typed length, and
     * the length travelled through two levels that declare nothing. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "label"), &label);
    CssComputedIs(label, "font-size", "20px");
    /* The provenance is the parent, not the declaration: what the label asked
     * for was *inheritance*, and the value it got is the body's, two levels up,
     * already computed. */
    CssComputedFlagsAre(label, "font-size", "inherited");
    /* `inherit` on a property that does **not** inherit by default still pulls
     * the parent's computed value — the keyword is what makes it happen, and
     * without it the label would have no `width` entry at all. */
    CssComputedIs(label, "width", "300px");
    CssComputedFlagsAre(label, "width", "inherited");
    value = CssComputedStyleComputedByName(label,
                                                     CssSliceFromText("font-size"));
    RoboCheckEqInt(value ? (int)value->kind : -1, RoboCssComputedValueLength);
    RoboCheckEqInt(value ? (int)value->unit : -1, (int)RoboCssUnitPx);
    RoboCheckEqInt(value ? (int)value->number : -1, 20);
    CssComputedIs(label, "display", "inline");
    /* Nothing declared a colour on the label, so it has the body's, inherited
     * and then computed. */
    CssComputedIs(label, "color", "#008000");
    CssComputedFlagsAre(label, "color", "inherited");
    /* A custom property is an opaque token sequence, and inheriting one does not
     * interpret it. */
    CssComputedIs(label, "--theme", "#0E7AFF");
    CssComputedFlagsAre(label, "--theme", "inherited+custom");

    /* A dimension is a length with a unit, not the four characters of `12px`. */
    CssDomCompute(&fixture, CssDomRefById(&fixture.dom, "DisplayName"), &field);
    CssComputedIs(field, "width", "12px");

    /* `color: nonsense` is a *wrong* value, and the style says so instead of
     * hiding the declaration or inventing a colour for it. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "button"), &button);
    CssComputedIs(button, "color", "unknown colour name");
    CssComputedFlagsAre(button, "color", "declared+invalid");
    RoboCheckEqInt(CssComputedStyleHasInvalid(button), 1);
    RoboCheck(CssComputedStyleIncompleteCount(button) >= 1u);
    /* One wrong value does not corrupt the rest of the style. `display` is
     * non-inherited and nothing declared it, so it has no *entry* — which is a
     * different answer from a wrong one, and the query door is where a
     * non-inherited property's "nothing declared it" answer lives. */
    CssComputedIs(button, "display", "<absent>");
    {
        CssComputedValue initial = RoboCssCleared;
        int fromInitial = 0;

        RoboCheckEqInt(CssStyleContextQueryComputedValue(
                              fixture.context, button, CssSliceFromText("display"),
                              &initial, &fromInitial, NULL),
                          RoboOk);
        RoboCheckEqInt(fromInitial, 1);
        /* The initial value arrives *typed*, through the property's own value
         * shape: since 9.12 that is the display vocabulary, so the door answers
         * with an enumerator rather than with the keyword's letters. */
        RoboCheckStrEq(CssComputedValueKindName(initial.kind), "display");
        RoboCheckEqInt(CssComputedValueDisplay(&initial),
                          (int)RoboCssDisplayInline);
        CssComputedValueDispose(&initial);
    }
    /* The inherited half of the style is present and unharmed. */
    CssComputedIs(button, "font-size", "20px");
    CssComputedFlagsAre(button, "font-size", "inherited");

    CssComputedStyleDestroy(button);
    CssComputedStyleDestroy(field);
    CssComputedStyleDestroy(label);
    CssComputedStyleDestroy(overview);
    CssComputedStyleDestroy(NULL);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssDomCascadeEnd(&fixture);
}

/**
 * `css.dom.computed.does_not_disturb_the_picture` — the visual vertical slice
 * with the whole CSS pipeline inserted into the middle of it.
 *
 * The document of record is painted, put through every stage 9.8 adds (a
 * stylesheet covering most of its elements, matched, cascaded, inherited and
 * *computed* for every element in the tree), and painted again. The two frames
 * must be byte-identical, and the tree itself must be untouched.
 *
 * This is the strongest form of "computing a style does not change what you
 * see", because the comparison is the pixels the production pipeline produces
 * rather than a claim about the tree it reads. A stage that mutated an attribute,
 * reordered a child, borrowed a slice past its source's life or left an
 * inherited value aliasing its parent's would fail here and nowhere else.
 */
static void TestCssDomComputedDoesNotDisturbThePicture(void)
{
    CssDomCascade fixture;
    RenderTarget *before = NULL;
    RenderTarget *after = NULL;
    CssStylesheetId sheet;
    uint64_t fingerprintBefore;
    CssElementRef ref;

    CssDomCascadeBegin(&fixture, DomCheckpointHtml());
    sheet = CssDomCascadeSheet(&fixture,
                                  "* { color: blue }\n"
                                  "html > body { font-size: 18px; line-height: 1.4 }\n"
                                  "body > * + * { margin: 4px }\n"
                                  "p { color: #336699 }\n"
                                  "#Overview { color: red !important }\n"
                                  "form > label { font-size: inherit; display: inline }\n"
                                  "input, textarea, select { width: calc(100% - 2px) }\n"
                                  "button { color: nonsense }\n"
                                  "body { --theme: blue }");
    fingerprintBefore = CssDomFingerprint(&fixture.dom);
    before = CssDomPicture();

    /* Every element, through all seven stages, twice over. */
    for (ref = 1u; ref <= fixture.dom.count; ref++) {
        CssComputedStyle *style = NULL;
        size_t pass;

        for (pass = 0u; pass < 2u; pass++) {
            CssDomCompute(&fixture, ref, &style);
            RoboCheck(CssComputedStyleCount(style) > 0u);
            CssComputedStyleDestroy(style);
            style = NULL;
        }
    }

    RoboCheckEqInt((int)CssDomFingerprint(&fixture.dom), (int)fingerprintBefore);
    after = CssDomPicture();
    RenderTargetsEqual(before, after, "computing a style changed the rendered frame");

    RenderTargetEnd(before);
    RenderTargetEnd(after);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssDomCascadeEnd(&fixture);
}

/**
 * `css.dom.unit.pipeline` — the unit and number vocabulary over the real tree.
 *
 * The CSS-side suite proves the vocabulary; this proves it survives a document.
 * Every family is declared somewhere on the checkpoint's own tree, and the two
 * halves of the phase's contract are asserted through the *computed style* rather
 * than through the parser: an absolute length lands converted, a contextual one
 * lands with its unit still live, and a percentage is still a percentage. The
 * unit is read off the typed record as well as off the text, because a value that
 * printed `3.5rem` while carrying `px` would pass a text-only test.
 */
static void TestCssDomUnitPipeline(void)
{
    CssDomCascade fixture;
    CssComputedStyle *bodyStyle = NULL;
    CssComputedStyle *paragraph = NULL;
    CssComputedStyle *field = NULL;
    CssComputedStyle *label = NULL;
    CssComputedStyle *area = NULL;
    CssComputedStyle *picker = NULL;
    CssComputedStyle *button = NULL;
    const CssComputedValue *value;
    CssStylesheetId sheet;

    CssDomCascadeBegin(&fixture, DomCheckpointHtml());
    sheet = CssDomCascadeSheet(&fixture,
                                  "body { width: 30em; font-size: 1.25rem; height: 50vh }\n"
                                  "p { width: 2.54cm }\n"
                                  "form { width: 6pc }\n"
                                  "input { width: 3.5rem; height: 2vi }\n"
                                  "label { width: 40q; font-size: inherit }\n"
                                  "textarea { width: 1e2px; height: 75% }\n"
                                  "select { width: 12dvmax }\n"
                                  "button { width: 10furlongs }");

    /* Every element this test reads really exists in the document, so an
     * assertion below is about a computed style rather than about a missing
     * element answering nothing. */
    RoboCheck(CssDomRefByTag(&fixture.dom, "body") != RoboCssElementRefNone);
    RoboCheck(CssDomRefByTag(&fixture.dom, "label") != RoboCssElementRefNone);
    RoboCheck(CssDomRefByTag(&fixture.dom, "textarea") != RoboCssElementRefNone);
    RoboCheck(CssDomRefByTag(&fixture.dom, "select") != RoboCssElementRefNone);

    /* Contextual units stay contextual, with the unit still live on the record —
     * which is what a later phase resolves and what converting here would
     * destroy. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "body"), &bodyStyle);
    CssComputedIs(bodyStyle, "width", "30em");
    CssComputedIs(bodyStyle, "font-size", "1.25rem");
    CssComputedIs(bodyStyle, "height", "50vh");
    value = CssComputedStyleComputedByName(bodyStyle,
                                                     CssSliceFromText("height"));
    RoboCheck(value != NULL);
    RoboCheckEqInt(value ? (int)value->kind : -1, RoboCssComputedValueLength);
    RoboCheckEqInt(value ? (int)value->unit : -1, (int)RoboCssUnitVh);
    RoboCheckEqInt(value ? (int)value->number : -1, 50);

    /* An absolute length is converted to the reference pixel wherever it was
     * written: `2.54cm` and `6pc` are 96px, which is the same number the wheel
     * of absolute units gives a metre. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "p"), &paragraph);
    CssComputedIs(paragraph, "width", "96px");
    value = CssComputedStyleComputedByName(paragraph,
                                                     CssSliceFromText("width"));
    RoboCheckEqInt(value ? (int)value->unit : -1, (int)RoboCssUnitPx);
    /* And a non-inherited property nobody declared has no entry at all, which is
     * a different answer from a wrong one. */
    CssComputedIs(paragraph, "height", "<absent>");
    {
        CssComputedStyle *formStyle = NULL;

        CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "form"), &formStyle);
        CssComputedIs(formStyle, "width", "96px");
        CssComputedStyleDestroy(formStyle);
    }

    /* The unit that is a quarter of a millimetre, and the axes and the dynamic
     * family: three of the forms 9.10 added, each carried rather than guessed. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "label"), &label);
    CssComputedIs(label, "width", "37.795276px");
    /* `font-size: inherit` takes the body's *computed* contextual length two
     * levels up, which is inheritance and the unit vocabulary meeting: the label
     * gets `1.25rem`, not a re-parse of `1.25rem` and not a pixel count. */
    CssComputedIs(label, "font-size", "1.25rem");
    CssComputedFlagsAre(label, "font-size", "inherited");

    CssDomCompute(&fixture, CssDomRefById(&fixture.dom, "DisplayName"), &field);
    CssComputedIs(field, "width", "3.5rem");
    CssComputedIs(field, "height", "2vi");

    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "textarea"), &area);
    CssComputedIs(area, "width", "100px");
    /* A percentage stays a percentage beside a converted length, because only
     * layout knows what it is a percentage *of*. */
    CssComputedIs(area, "height", "75%");
    value = CssComputedStyleComputedByName(area,
                                                     CssSliceFromText("height"));
    RoboCheckEqInt(value ? (int)value->kind : -1,
                      (int)RoboCssComputedValuePercentage);

    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "select"), &picker);
    CssComputedIs(picker, "width", "12dvmax");

    /* An unknown unit is *reported* rather than read as a bare number, and the
     * rest of the element's style is unharmed. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "button"), &button);
    CssComputedIs(button, "width", "unknown length unit");
    CssComputedFlagsAre(button, "width", "declared+invalid");
    /* The inherited half is present: the button declares no font size, so it has
     * the body's contextual length. */
    CssComputedIs(button, "font-size", "1.25rem");
    CssComputedFlagsAre(button, "font-size", "inherited");

    /* Deterministic over the real tree: the same element computed twice yields
     * the same typed unit and the same magnitude. The comparison is on the
     * *record* rather than on the text, so a value that printed the same while
     * carrying a different unit would still fail. */
    {
        CssComputedStyle *again = NULL;
        const CssComputedValue *first = NULL;
        const CssComputedValue *second = NULL;

        CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "body"), &again);
        first = CssComputedStyleComputedByName(bodyStyle,
                                                        CssSliceFromText("font-size"));
        second = CssComputedStyleComputedByName(again,
                                                          CssSliceFromText("font-size"));
        RoboCheck(first != NULL && second != NULL);
        if (first && second) {
            RoboCheckEqInt((int)second->kind, (int)first->kind);
            RoboCheckEqInt((int)second->unit, (int)first->unit);
            RoboCheckEqInt((int)second->number, (int)first->number);
        }
        RoboCheckEqInt((int)second->unit, (int)RoboCssUnitRem);
        RoboCheckEqInt((int)second->number, 1);
        CssComputedStyleDestroy(again);
    }

    CssComputedStyleDestroy(button);
    CssComputedStyleDestroy(picker);
    CssComputedStyleDestroy(area);
    CssComputedStyleDestroy(field);
    CssComputedStyleDestroy(label);
    CssComputedStyleDestroy(paragraph);
    CssComputedStyleDestroy(bodyStyle);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssDomCascadeEnd(&fixture);
}

/**
 * `css.dom.box.pipeline` — the box model over the real tree.
 *
 * The CSS-side suite proves the vocabulary and the expansion; this proves they
 * survive a document, which is where the one thing that could go wrong at scale
 * would show: a shorthand is *one* declaration standing for *four* properties, and
 * the four have to arrive on elements the author never named individually. Every
 * arity is declared somewhere on the checkpoint's own tree, and the competition
 * between a shorthand and a longhand is settled on a real element rather than on a
 * hand-built cascade. It closes on the phase's acceptance sheet — one rule holding
 * a width, a height, both shorthands, a border width and a colour — because that is
 * the shape that proves the value layer reads dimensions and colours in the same
 * pass, and because a colour is the one family this phase must not have broken.
 */
static void TestCssDomBoxPipeline(void)
{
    CssDomCascade fixture;
    CssComputedStyle *bodyStyle = NULL;
    CssComputedStyle *paragraph = NULL;
    CssComputedStyle *label = NULL;
    CssComputedStyle *formStyle = NULL;
    CssComputedStyle *area = NULL;
    CssComputedStyle *field = NULL;
    CssComputedStyle *picker = NULL;
    CssComputedStyle *button = NULL;
    CssComputedStyle *acceptance = NULL;
    CssStylesheetId sheet;
    CssStylesheetId acceptanceSheet;

    CssDomCascadeBegin(&fixture, DomCheckpointHtml());
    sheet = CssDomCascadeSheet(&fixture,
                                  "body { margin: 0 auto }\n"
                                  "p { padding: 8px 12px 4px }\n"
                                  "label { border-width: thin medium thick }\n"
                                  "form { box-sizing: border-box }\n"
                                  "textarea { margin: 10%; padding: 2em }\n"
                                  "input { margin: 1px }\n"
                                  "select { padding: inherit }\n"
                                  "button { padding: 1px 2px; padding-left: 9px }");

    RoboCheck(CssDomRefByTag(&fixture.dom, "body") != RoboCssElementRefNone);
    RoboCheck(CssDomRefByTag(&fixture.dom, "label") != RoboCssElementRefNone);
    RoboCheck(CssDomRefByTag(&fixture.dom, "textarea") != RoboCssElementRefNone);

    /* Two components: vertical zero, horizontal `auto` — and `auto` survives as
     * the keyword it is rather than becoming a number. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "body"), &bodyStyle);
    CssComputedIs(bodyStyle, "margin-top", "0px");
    CssComputedIs(bodyStyle, "margin-right", "auto");
    CssComputedIs(bodyStyle, "margin-left", "auto");
    CssComputedFlagsAre(bodyStyle, "margin-left", "declared");
    /* The shorthand's own entry is there too, with the author's text. */
    CssComputedIs(bodyStyle, "margin", "0 auto");

    /* Three components: the arity where top and bottom differ and left borrows
     * right, which is the row a hand-written branch gets wrong. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "p"), &paragraph);
    CssComputedIs(paragraph, "padding-top", "8px");
    CssComputedIs(paragraph, "padding-right", "12px");
    CssComputedIs(paragraph, "padding-bottom", "4px");
    CssComputedIs(paragraph, "padding-left", "12px");

    /* Border widths keep their keywords, because what `medium` is in pixels is a
     * used value and this phase computes no used value. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "label"), &label);
    CssComputedIs(label, "border-top-width", "thin");
    CssComputedIs(label, "border-right-width", "medium");
    CssComputedIs(label, "border-bottom-width", "thick");
    CssComputedIs(label, "border-left-width", "medium");

    /* `box-sizing` is a value model with no layout behind it: the keyword is what
     * a declaration means here, and which box a width applies to is a later
     * phase's question. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "form"), &formStyle);
    CssComputedIs(formStyle, "box-sizing", "border-box");

    /* A percentage and a contextual length are box edges like any other, and they
     * stay what they are: only layout knows what `10%` is a percentage of, and
     * only a font knows what `2em` is. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "textarea"), &area);
    CssComputedIs(area, "margin-top", "10%");
    CssComputedIs(area, "margin-left", "10%");
    CssComputedIs(area, "padding-top", "2em");

    /* The uniform spelling, on an element the sheet names by type. */
    CssDomCompute(&fixture, CssDomRefById(&fixture.dom, "DisplayName"), &field);
    CssComputedIs(field, "margin-top", "1px");
    CssComputedIs(field, "margin-bottom", "1px");

    /* `padding: inherit` on a property that does not inherit, with no padding on
     * the ancestor chain: the longhand's own initial value, which is zero — and
     * not the parent's box, because there is no such thing to take. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "select"), &picker);
    CssComputedIs(picker, "padding-top", "0px");
    CssComputedFlagsAre(picker, "padding-top", "default");

    /* A shorthand and a longhand for the same edge, on one element, decided by
     * document order: `padding-left: 9px` comes later than `padding: 1px 2px`, so
     * the left edge is the longhand's and the other three are the shorthand's. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "button"), &button);
    CssComputedIs(button, "padding-top", "1px");
    CssComputedIs(button, "padding-right", "2px");
    CssComputedIs(button, "padding-bottom", "1px");
    CssComputedIs(button, "padding-left", "9px");

    /* The phase's own acceptance sheet, in one rule on one element: a width, a
     * height, both shorthands, a border width and a colour. The colour is here
     * because it is the one value family this phase did not add and must not
     * have broken — and because one rule holding a dimension, a percentage, two
     * keywords and a colour is the shape that exposes a value layer which reads
     * only one kind. This sheet is added second, so it is also the cross-sheet
     * source-order case: it outranks the `textarea` rule above it. */
    acceptanceSheet = CssDomCascadeSheet(&fixture,
                                             "textarea { width: 200px; "
                                             "height: 100px; margin: 10px 20px; "
                                             "padding: 8px 12px; "
                                             "border-width: 2px; "
                                             "color: #336699 }");
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "textarea"),
                    &acceptance);
    CssComputedIs(acceptance, "width", "200px");
    CssComputedIs(acceptance, "height", "100px");
    CssComputedIs(acceptance, "margin-top", "10px");
    CssComputedIs(acceptance, "margin-right", "20px");
    CssComputedIs(acceptance, "margin-bottom", "10px");
    CssComputedIs(acceptance, "margin-left", "20px");
    CssComputedIs(acceptance, "padding-top", "8px");
    CssComputedIs(acceptance, "padding-right", "12px");
    CssComputedIs(acceptance, "padding-bottom", "8px");
    CssComputedIs(acceptance, "padding-left", "12px");
    CssComputedIs(acceptance, "border-top-width", "2px");
    CssComputedIs(acceptance, "border-right-width", "2px");
    CssComputedIs(acceptance, "color", "#336699");
    RoboCheckEqInt(CssStylesheetDestroy(acceptanceSheet, NULL),
                      RoboOk);

    CssComputedStyleDestroy(acceptance);
    CssComputedStyleDestroy(button);
    CssComputedStyleDestroy(picker);
    CssComputedStyleDestroy(field);
    CssComputedStyleDestroy(area);
    CssComputedStyleDestroy(formStyle);
    CssComputedStyleDestroy(label);
    CssComputedStyleDestroy(paragraph);
    CssComputedStyleDestroy(bodyStyle);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssDomCascadeEnd(&fixture);
}

/**
 * `css.dom.display.pipeline` — the display vocabulary over the real tree.
 *
 * The CSS-side suite proves the vocabulary and the value shape; this proves they
 * survive a document, which is where the two facts the phase is built on have to
 * hold at once: a declared `display` arrives as a typed value on elements the
 * sheet names, and an element the sheet does *not* name has **no entry at all**
 * because `display` is non-inherited and 9.7 materialises initial values for the
 * inherited-by-default set only. The query door is what answers that second case,
 * with the property's own initial value and `fromInitial` set — so the pair of
 * assertions below is the whole interface a layout phase consumes.
 */
static void TestCssDomDisplayPipeline(void)
{
    CssDomCascade fixture;
    CssComputedStyle *bodyStyle = NULL;
    CssComputedStyle *link = NULL;
    CssComputedStyle *caption = NULL;
    CssComputedStyle *notes = NULL;
    CssComputedStyle *heading = NULL;
    CssComputedValue queried = RoboCssCleared;
    CssStylesheetId sheet;
    int fromInitial = -1;

    CssDomCascadeBegin(&fixture, DomCheckpointHtml());
    sheet = CssDomCascadeSheet(&fixture,
                                  "body { display: block }\n"
                                  "a { display: inline }\n"
                                  "label { display: inline-block }\n"
                                  "#Notes { display: none }\n");

    RoboCheck(CssDomRefByTag(&fixture.dom, "body") != RoboCssElementRefNone);
    RoboCheck(CssDomRefById(&fixture.dom, "Notes") != RoboCssElementRefNone);

    /* One value per level of the box model's own vocabulary, each read as a
     * typed enumerator rather than as the keyword's letters. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "body"), &bodyStyle);
    CssComputedIs(bodyStyle, "display", "block");
    {
        const CssComputedValue *value =
            CssComputedStyleComputedByName(bodyStyle,
                                                    CssSliceFromText("display"));

        CssDisplay display;

        RoboCheckEqInt(value->kind, (int)RoboCssComputedValueDisplay);
        display = CssComputedValueDisplay(value);
        RoboCheckEqInt(CssDisplayIsBlockLevel(display), 1);
        RoboCheckEqInt(CssDisplayGeneratesBox(display), 1);
    }

    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "a"), &link);
    CssComputedIs(link, "display", "inline");
    RoboCheckEqInt(CssDisplayIsInlineLevel(
                          CssComputedValueDisplay(
                              CssComputedStyleComputedByName(
                                  link, CssSliceFromText("display")))),
                      1);

    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "label"), &caption);
    CssComputedIs(caption, "display", "inline-block");
    RoboCheckEqInt(CssDisplayIsInlineBlock(
                          CssComputedValueDisplay(
                              CssComputedStyleComputedByName(
                                  caption, CssSliceFromText("display")))),
                      1);

    /* `display: none` on a real element: a decided value, not a missing one, and
     * not a geometry — the element's own `textarea` box is untouched beside it. */
    CssDomCompute(&fixture, CssDomRefById(&fixture.dom, "Notes"), &notes);
    CssComputedIs(notes, "display", "none");
    CssComputedFlagsAre(notes, "display", "declared");
    RoboCheckEqInt(CssComputedStyleHasInvalid(notes), 0);
    RoboCheckEqInt(CssDisplayIsNone(
                          CssComputedValueDisplay(
                              CssComputedStyleComputedByName(
                                  notes, CssSliceFromText("display")))),
                      1);

    /* An element the sheet does not name has no entry — `display` does not
     * inherit, so the `body` above handed nothing down — and the query door
     * answers with the property's initial value, marked as such. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "h1"), &heading);
    CssComputedIs(heading, "display", "<absent>");
    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          fixture.context, heading,
                          CssSliceFromText("display"), &queried,
                          &fromInitial, NULL),
                      RoboOk);
    RoboCheckEqInt(fromInitial, 1);
    RoboCheckEqInt(queried.kind, (int)RoboCssComputedValueDisplay);
    RoboCheckEqInt(CssComputedValueDisplay(&queried),
                      (int)RoboCssDisplayInline);
    CssComputedValueDispose(&queried);

    /* And an invalid value on a real element is reported rather than replaced. */
    {
        CssComputedStyle *odd = NULL;
        CssStylesheetId bad = CssDomCascadeSheet(
            &fixture, "select { display: bogus }");

        CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "select"), &odd);
        CssComputedIs(odd, "display", "not a display value");
        CssComputedFlagsAre(odd, "display", "declared+invalid");
        RoboCheckEqInt(CssComputedStyleHasInvalid(odd), 1);
        CssComputedStyleDestroy(odd);
        RoboCheckEqInt(CssStylesheetDestroy(bad, NULL), RoboOk);
    }

    CssComputedStyleDestroy(heading);
    CssComputedStyleDestroy(notes);
    CssComputedStyleDestroy(caption);
    CssComputedStyleDestroy(link);
    CssComputedStyleDestroy(bodyStyle);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssDomCascadeEnd(&fixture);
}

/**
 * `css.dom.position.pipeline` — the positioning family over the real tree.
 *
 * The CSS-side suite proves the vocabulary, the four insets and `z-index` as
 * values; this proves the family survives a document, which is where three facts
 * have to hold at once. A declared `position` arrives as a typed enumerator on the
 * elements the sheet names. The four offsets and the level arrive *beside* it —
 * a percentage which stays a percentage, an `auto` that stays a keyword, a whole
 * number — which is the whole reason the insets reuse the box model's shape
 * instead of getting one of their own. And an element the sheet does **not** name
 * has no entry for any of the six, because none of them inherits, so the query
 * door is what answers, with the property's own initial value and `fromInitial`
 * set.
 *
 * That last pair is the interface a layout phase consumes, and the assertion on
 * it is deliberately the same shape as the display pipeline's: `static` and `auto`
 * are *values* and not failures, and the difference between "the author said
 * static" and "the author said nothing" is the flag, not the value.
 */
static void TestCssDomPositionPipeline(void)
{
    CssDomCascade fixture;
    CssComputedStyle *bodyStyle = NULL;
    CssComputedStyle *heading = NULL;
    CssComputedStyle *button = NULL;
    CssComputedStyle *paragraph = NULL;
    CssComputedValue queried = RoboCssCleared;
    CssStylesheetId sheet;
    int fromInitial = -1;

    CssDomCascadeBegin(&fixture, DomCheckpointHtml());
    sheet = CssDomCascadeSheet(&fixture,
                                  "body { position: relative; z-index: 0 }\n"
                                  "h1 { position: absolute; top: 25%; left: -8px;"
                                  " z-index: -1 }\n"
                                  "button { position: sticky; top: 0; right: 2em;"
                                  " bottom: auto }\n");

    RoboCheck(CssDomRefByTag(&fixture.dom, "body") != RoboCssElementRefNone);
    RoboCheck(CssDomRefByTag(&fixture.dom, "button") != RoboCssElementRefNone);

    /* `relative` is in flow *and* positioned, which is the pair that reads as a
     * contradiction until the two columns are read separately — and the reason the
     * row carries both. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "body"), &bodyStyle);
    CssComputedIs(bodyStyle, "position", "relative");
    CssComputedIs(bodyStyle, "z-index", "0");
    {
        const CssComputedValue *value =
            CssComputedStyleComputedByName(
                bodyStyle, CssSliceFromText("position"));
        CssPositioning positioning;

        RoboCheckEqInt(value->kind, (int)RoboCssComputedValuePosition);
        positioning = CssComputedValuePosition(value);
        RoboCheckEqInt(CssPositioningIsInFlow(positioning), 1);
        RoboCheckEqInt(CssPositioningIsPositioned(positioning), 1);
        RoboCheckEqInt(CssPositioningIsOutOfFlow(positioning), 0);
    }

    /* `absolute` with a percentage, a negative length and a negative level, all
     * read beside the enumerator that gives them meaning. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "h1"), &heading);
    CssComputedIs(heading, "position", "absolute");
    CssComputedFlagsAre(heading, "position", "declared");
    CssComputedIs(heading, "top", "25%");
    CssComputedIs(heading, "left", "-8px");
    CssComputedIs(heading, "z-index", "-1");
    RoboCheckEqInt(CssPositioningIsOutOfFlow(
                          CssComputedValuePosition(
                              CssComputedStyleComputedByName(
                                  heading, CssSliceFromText("position")))),
                      1);
    /* The percentage is a *percentage* and the `auto` below is a *keyword* —
     * different kinds, which is what stops a later phase reading `auto` as a
     * zero-length offset. */
    {
        const CssComputedValue *value =
            CssComputedStyleComputedByName(
                heading, CssSliceFromText("top"));

        RoboCheckEqInt(value->kind, (int)RoboCssComputedValuePercentage);
    }

    /* `sticky` is represented and classified like any other value, and it is
     * *not* behaved on: the offsets beside it are offsets, and nothing here
     * scrolls. That is the phase's documented boundary, asserted rather than
     * assumed. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "button"), &button);
    CssComputedIs(button, "position", "sticky");
    RoboCheckEqInt(CssComputedValuePosition(
                          CssComputedStyleComputedByName(
                              button, CssSliceFromText("position"))),
                      (int)RoboCssPositioningSticky);
    RoboCheckEqInt(CssComputedStyleHasInvalid(button), 0);
    CssComputedIs(button, "top", "0px");
    CssComputedIs(button, "right", "2em");
    CssComputedIs(button, "bottom", "auto");
    {
        const CssComputedValue *value =
            CssComputedStyleComputedByName(
                button, CssSliceFromText("bottom"));

        RoboCheckEqInt(value->kind, (int)RoboCssComputedValueKeyword);
    }

    /* An element the sheet does not name has no entry for any of the six — none
     * of them inherits, so the `body` above handed nothing down — and the query
     * door answers each with the property's own initial value, marked as such. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "p"), &paragraph);
    CssComputedIs(paragraph, "position", "<absent>");
    CssComputedIs(paragraph, "z-index", "<absent>");
    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          fixture.context, paragraph,
                          CssSliceFromText("position"), &queried,
                          &fromInitial, NULL),
                      RoboOk);
    RoboCheckEqInt(fromInitial, 1);
    RoboCheckEqInt(queried.kind, (int)RoboCssComputedValuePosition);
    RoboCheckEqInt(CssComputedValuePosition(&queried),
                      (int)RoboCssPositioningStatic);
    /* Static is a *value* here and not an absence: the box is in flow and not
     * positioned, so an offset on it would not apply. */
    RoboCheckEqInt(CssPositioningIsPositioned(
                          CssComputedValuePosition(&queried)),
                      0);
    RoboCheckEqInt(CssPositioningIsInFlow(
                          CssComputedValuePosition(&queried)),
                      1);
    CssComputedValueDispose(&queried);

    {
        static const char *const names[] = { "top", "right", "bottom", "left" };
        size_t index;

        for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
            CssComputedIs(paragraph, names[index], "<absent>");
            RoboCheckEqInt(CssStyleContextQueryComputedValue(
                                  fixture.context, paragraph,
                                  CssSliceFromText(names[index]), &queried,
                                  &fromInitial, NULL),
                              RoboOk);
            RoboCheckEqInt(fromInitial, 1);
            /* An `auto` inset is not a zero-length offset: it is the keyword, and
             * the kind is what says so. */
            RoboCheckEqInt(queried.kind, (int)RoboCssComputedValueKeyword);
            CssComputedValueDispose(&queried);
        }
    }

    /* And an invalid value on a real element is reported rather than replaced. */
    {
        CssComputedStyle *odd = NULL;
        CssStylesheetId bad = CssDomCascadeSheet(
            &fixture,
            "select { position: banana; top: nope; z-index: 1.5 }");

        CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "select"), &odd);
        CssComputedIs(odd, "position", "not a position value");
        CssComputedIs(odd, "top", "not a length, a percentage or auto");
        CssComputedIs(odd, "z-index", "not a whole number");
        CssComputedFlagsAre(odd, "position", "declared+invalid");
        RoboCheckEqInt(CssComputedStyleHasInvalid(odd), 1);
        CssComputedStyleDestroy(odd);
        RoboCheckEqInt(CssStylesheetDestroy(bad, NULL), RoboOk);
    }

    CssComputedStyleDestroy(paragraph);
    CssComputedStyleDestroy(button);
    CssComputedStyleDestroy(heading);
    CssComputedStyleDestroy(bodyStyle);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssDomCascadeEnd(&fixture);
}

/*
 * `css.dom.background.pipeline` — the background family over the real tree.
 *
 * The CSS-side suite proves the six longhands, the two closed vocabularies and
 * the two pairs as values; this proves the family survives a *document*, which is
 * where four facts have to hold at once. A declared longhand arrives as a typed
 * value on the elements the sheet names — a colour on one, a preserved image
 * reference on another, a pair of components on a third. The `background`
 * shorthand *distributes* over all six, so an element that wrote one declaration
 * has six entries. An element the sheet names for one property has **no entry**
 * for the other five — none of them inherits — so the query door is what answers, 
 * with the property's own initial value and `fromInitial` set. And a percentage
 * stays a percentage: `25% 50%` is a pair of percentages because only the phase
 * with a box can say 25% of what.
 *
 * That last pair — the query door and `fromInitial` — is the interface a paint
 * phase consumes, and it is asserted in the same shape the display, box and
 * positioning pipelines assert it.
 */
static void TestCssDomBackgroundPipeline(void)
{
    CssDomCascade fixture;
    CssComputedStyle *bodyStyle = NULL;
    CssComputedStyle *heading = NULL;
    CssComputedStyle *button = NULL;
    CssComputedStyle *paragraph = NULL;
    CssComputedValue queried = RoboCssCleared;
    CssStylesheetId sheet;
    int fromInitial = -1;

    CssDomCascadeBegin(&fixture, DomCheckpointHtml());
    sheet = CssDomCascadeSheet(&fixture,
                                  "body { background-color: #336699;"
                                  " background-repeat: repeat-x }\n"
                                  "h1 { background-color: #ff0000;"
                                  " background-image: url(\"Heading.png\");"
                                  " background-position: 25% 50%;"
                                  " background-size: cover;"
                                  " background-repeat: no-repeat;"
                                  " background-attachment: fixed }\n"
                                  "button { background: #ffffff no-repeat }\n"
                                  "p { background-color: transparent }\n");

    RoboCheck(CssDomRefByTag(&fixture.dom, "body") != RoboCssElementRefNone);
    RoboCheck(CssDomRefByTag(&fixture.dom, "h1") != RoboCssElementRefNone);
    RoboCheck(CssDomRefByTag(&fixture.dom, "button") != RoboCssElementRefNone);

    /* A colour and a repeat, and nothing else: the other four have no entry at
     * all, which is the difference between "the author said nothing" and "the
     * author said the initial value". */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "body"), &bodyStyle);
    CssComputedIs(bodyStyle, "background-color", "#336699");
    CssComputedIs(bodyStyle, "background-repeat", "repeat-x");
    CssComputedFlagsAre(bodyStyle, "background-color", "declared");
    CssComputedIs(bodyStyle, "background-image", "<absent>");
    CssComputedIs(bodyStyle, "background-position", "<absent>");
    CssComputedIs(bodyStyle, "background-size", "<absent>");
    CssComputedIs(bodyStyle, "background-attachment", "<absent>");
    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          fixture.context, bodyStyle,
                          CssSliceFromText("background-image"), &queried,
                          &fromInitial, NULL),
                      RoboOk);
    RoboCheckEqInt(fromInitial, 1);
    RoboCheckEqInt(queried.kind, (int)RoboCssComputedValueKeyword);
    {
        char text[RoboCssComputedValueTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssComputedValueWrite(&queried, text, sizeof(text),
                                                       &length, NULL),
                          RoboOk);
        RoboCheckStrEq(text, "none");
    }
    CssComputedValueDispose(&queried);
    /* The two shapes that are not words are answered by the query door as well:
     * `0% 0%` is a *pair* and `auto` is not a length. */
    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          fixture.context, bodyStyle,
                          CssSliceFromText("background-position"), &queried,
                          &fromInitial, NULL),
                      RoboOk);
    RoboCheckEqInt(fromInitial, 1);
    RoboCheckEqInt(queried.kind,
                      (int)RoboCssComputedValueBackgroundPosition);
    RoboCheckEqInt(
        CssComputedValueBackgroundPosition(&queried).horizontal.kind,
        RoboCssBackgroundComponentPercentage);
    CssComputedValueDispose(&queried);
    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          fixture.context, bodyStyle,
                          CssSliceFromText("background-size"), &queried,
                          &fromInitial, NULL),
                      RoboOk);
    RoboCheckEqInt(fromInitial, 1);
    RoboCheckEqInt(queried.kind, (int)RoboCssComputedValueBackgroundSize);
    RoboCheckEqInt(CssComputedValueBackgroundSize(&queried).kind,
                      RoboCssBackgroundSizeAuto);
    CssComputedValueDispose(&queried);

    /* All six on one element, each typed: an image this build carries and
     * *flags*, a pair of percentages, a whole-value size keyword, a closed-vocabulary
     * repeat and attachment, and a colour. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "h1"), &heading);
    CssComputedIs(heading, "background-color", "#ff0000");
    CssComputedIs(heading, "background-image", "url(\"Heading.png\")");
    CssComputedIs(heading, "background-position", "25% 50%");
    CssComputedIs(heading, "background-size", "cover");
    CssComputedIs(heading, "background-repeat", "no-repeat");
    CssComputedIs(heading, "background-attachment", "fixed");
    RoboCheckEqInt(CssComputedStyleHasInvalid(heading), 0);
    {
        const CssComputedValue *value =
            CssComputedStyleComputedByName(
                heading, CssSliceFromText("background-image"));

        RoboCheckEqInt(value->kind, (int)RoboCssComputedValuePreserved);
        /* Carried *and* flagged: a consumer that read the tokens without the flag
         * would be reading a reference as if it were an image. */
        RoboCheckEqInt(value->unresolved, 1u);
    }
    {
        const CssComputedValue *value =
            CssComputedStyleComputedByName(
                heading, CssSliceFromText("background-position"));
        CssBackgroundPosition position;

        RoboCheckEqInt(value->kind,
                          (int)RoboCssComputedValueBackgroundPosition);
        position = CssComputedValueBackgroundPosition(value);
        /* Two percentages, and the *horizontal* one is the first the author
         * wrote — the order is the grammar's, and it survives the document. */
        RoboCheckEqInt(position.horizontal.kind,
                          RoboCssBackgroundComponentPercentage);
        RoboCheckEqInt(position.vertical.kind,
                          RoboCssBackgroundComponentPercentage);
    }
    {
        const CssComputedValue *value =
            CssComputedStyleComputedByName(
                heading, CssSliceFromText("background-size"));

        RoboCheckEqInt(value->kind, (int)RoboCssComputedValueBackgroundSize);
        RoboCheckEqInt(CssComputedValueBackgroundSize(value).kind,
                          RoboCssBackgroundSizeCover);
    }

    /* The shorthand: one declaration, six entries — and the ones it did not name
     * are the properties' own initial values rather than absent, which is what
     * makes `background: #ffffff no-repeat` mean "just this". */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "button"), &button);
    CssComputedIs(button, "background-color", "#ffffff");
    CssComputedIs(button, "background-repeat", "no-repeat");
    CssComputedIs(button, "background-image", "none");
    CssComputedIs(button, "background-position", "0% 0%");
    CssComputedIs(button, "background-size", "auto");
    CssComputedIs(button, "background-attachment", "scroll");
    CssComputedFlagsAre(button, "background-image", "declared");
    /* The shorthand's own entry is the author's text, preserved: nothing here
     * rewrites a declaration the consumer might want to read back. */
    CssComputedIs(button, "background", "#ffffff no-repeat");

    /* And nothing leaked: a `p` that declared only a colour has only a colour. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "p"), &paragraph);
    CssComputedIs(paragraph, "background-color", "#00000000");
    CssComputedIs(paragraph, "background-image", "<absent>");
    CssComputedIs(paragraph, "background-repeat", "<absent>");

    /* An invalid value on a real element is reported rather than replaced, and
     * the five refusals stay five sentences. */
    {
        CssComputedStyle *odd = NULL;
        CssStylesheetId bad = CssDomCascadeSheet(
            &fixture,
            "select { background-image: banana; background-position: banana;"
            " background-size: banana; background-repeat: banana;"
            " background-attachment: floating; background-color: banana }");

        CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "select"), &odd);
        CssComputedIs(odd, "background-image", "not a background image");
        CssComputedIs(odd, "background-position", "not a background position");
        CssComputedIs(odd, "background-size", "not a background size");
        CssComputedIs(odd, "background-repeat", "not a background repeat value");
        CssComputedIs(odd, "background-attachment", "not a background attachment value");
        /* The colour module's own sentence, which is 9.9's and not this phase's:
         * `background-color` keeps the shape 9.9 gave it. */
        CssComputedIs(odd, "background-color", "unknown colour name");
        RoboCheckEqInt(CssComputedStyleHasInvalid(odd), 1);
        CssComputedStyleDestroy(odd);
        RoboCheckEqInt(CssStylesheetDestroy(bad, NULL), RoboOk);
    }

    CssComputedStyleDestroy(paragraph);
    CssComputedStyleDestroy(button);
    CssComputedStyleDestroy(heading);
    CssComputedStyleDestroy(bodyStyle);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssDomCascadeEnd(&fixture);
}

/*
 * `css.dom.border.pipeline` — the border family over the real tree.
 *
 * Four things have to hold at once over a document, and none of them is visible
 * in the CSS-side suite alone. The `border` shorthand *distributes* over all
 * twelve longhands of four sides, so one declaration leaves twelve entries. A
 * declared value arrives **typed** — a style as a vocabulary row, a width as a
 * length, a colour as a colour — on the elements the sheet names and on no
 * others, because nothing in the family inherits. The positional rows
 * (`border-width`, `border-style`, `border-color`) split one to four components
 * over four sides by the box model's own mapping.
 *
 * And the one the phase turns on: a border colour is a **reference**. An element
 * whose only border declaration is `border: 1px solid` does not get `transparent`,
 * does not get the word `currentcolor`, and does not get its parent's colour — it
 * gets *its own* colour, through both doors: the entry the shorthand materialised
 * and the query a paint phase will actually call.
 */
static void TestCssDomBorderPipeline(void)
{
    CssDomCascade fixture;
    CssComputedStyle *bodyStyle = NULL;
    CssComputedStyle *heading = NULL;
    CssComputedStyle *button = NULL;
    CssComputedStyle *paragraph = NULL;
    CssComputedValue queried = RoboCssCleared;
    CssStylesheetId sheet;
    int fromInitial = -1;

    CssDomCascadeBegin(&fixture, DomCheckpointHtml());
    sheet = CssDomCascadeSheet(&fixture,
                                  "body { color: #336699; border: 1px solid }\n"
                                  "h1 { border: 3px double #ff0000;"
                                  " border-left-color: transparent }\n"
                                  "button { border-width: 1px 2px;"
                                  " border-style: solid dashed;"
                                  " border-color: red blue }\n"
                                  "p { border-top-color: currentcolor }\n");

    RoboCheck(CssDomRefByTag(&fixture.dom, "body") != RoboCssElementRefNone);
    RoboCheck(CssDomRefByTag(&fixture.dom, "h1") != RoboCssElementRefNone);
    RoboCheck(CssDomRefByTag(&fixture.dom, "button") != RoboCssElementRefNone);

    /* One declaration, twelve longhands — and the colour it did not write is the
     * element's own, which is what makes `border: 1px solid` a visible border
     * rather than an invisible one. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "body"), &bodyStyle);
    CssComputedIs(bodyStyle, "border-top-width", "1px");
    CssComputedIs(bodyStyle, "border-bottom-width", "1px");
    CssComputedIs(bodyStyle, "border-left-style", "solid");
    CssComputedIs(bodyStyle, "border-top-color", "#336699");
    CssComputedIs(bodyStyle, "border-left-color", "#336699");
    CssComputedFlagsAre(bodyStyle, "border-top-width", "declared");
    CssComputedIs(bodyStyle, "border", "1px solid");
    RoboCheckEqInt(CssComputedStyleHasInvalid(bodyStyle), 0);

    /* The width is a length and the style is a vocabulary row, not a word: the two
     * shapes a layout phase dispatches on are typed values on a real element. */
    {
        const CssComputedValue *width =
            CssComputedStyleComputedByName(
                bodyStyle, CssSliceFromText("border-top-width"));
        const CssComputedValue *style =
            CssComputedStyleComputedByName(
                bodyStyle, CssSliceFromText("border-top-style"));

        RoboCheckEqInt(width->kind, (int)RoboCssComputedValueLength);
        RoboCheckEqInt(style->kind, (int)RoboCssComputedValueBorderStyle);
        RoboCheckEqInt(CssComputedValueBorderStyle(style),
                          RoboCssBorderStyleSolid);
    }

    /* `transparent` is a colour and not an absence: the four longhands of one side
     * can disagree, and `transparent` survives as the four-zero record. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "h1"), &heading);
    CssComputedIs(heading, "border-top-color", "#ff0000");
    CssComputedIs(heading, "border-left-color", "#00000000");
    CssComputedIs(heading, "border-top-style", "double");
    CssComputedIs(heading, "border-bottom-width", "3px");

    /* The positional rows: one component, then two, over four sides by the box
     * model's mapping — two widths, two styles, two colours on four sides each. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "button"), &button);
    CssComputedIs(button, "border-top-width", "1px");
    CssComputedIs(button, "border-right-width", "2px");
    CssComputedIs(button, "border-bottom-width", "1px");
    CssComputedIs(button, "border-left-width", "2px");
    CssComputedIs(button, "border-top-style", "solid");
    CssComputedIs(button, "border-right-style", "dashed");
    CssComputedIs(button, "border-left-color", "#0000ff");
    CssComputedIs(button, "border", "<absent>");

    /* A declared `currentcolor` is the element's own colour, resolved once — and on
     * a `p` that is not black and not the initial value: `color` *inherits* from the
     * `body` rule above, so the reference resolves through inheritance rather than
     * through a default. One assertion, three rules. */
    CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "p"), &paragraph);
    CssComputedIs(paragraph, "border-top-color", "#336699");
    CssComputedIs(paragraph, "border-top-width", "<absent>");
    /* ...and the door a paint phase calls answers the same thing for a longhand
     * the author never mentioned: nothing inherits, so there is no entry, so the
     * property's *initial value* is computed here — and its initial value is the
     * reference, which resolves against this element's own colour. */
    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          fixture.context, paragraph,
                          CssSliceFromText("border-left-color"), &queried,
                          &fromInitial, NULL),
                      RoboOk);
    RoboCheckEqInt(fromInitial, 1);
    RoboCheckEqInt(queried.kind, (int)RoboCssComputedValueColor);
    RoboCheckEqInt(queried.unresolved, 0u);
    {
        char text[RoboCssComputedValueTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssComputedValueWrite(&queried, text, sizeof(text),
                                                       &length, NULL),
                          RoboOk);
        RoboCheckStrEq(text, "#336699");
    }
    CssComputedValueDispose(&queried);

    /* Every refusal is its own sentence rather than one fallback, and a bad border
     * declaration never replaces a good one or crashes the walk. */
    {
        CssComputedStyle *odd = NULL;
        CssStylesheetId bad = CssDomCascadeSheet(
            &fixture,
            "select { border-top-width: banana; border-top-style: banana;"
            " border-top-color: banana }");

        CssDomCompute(&fixture, CssDomRefByTag(&fixture.dom, "select"), &odd);
        CssComputedIs(odd, "border-top-width", "not a border width keyword");
        CssComputedIs(odd, "border-top-style", "not a border style value");
        CssComputedIs(odd, "border-top-color", "unknown colour name");
        RoboCheckEqInt(CssComputedStyleHasInvalid(odd), 1);
        CssComputedStyleDestroy(odd);
        RoboCheckEqInt(CssStylesheetDestroy(bad, NULL), RoboOk);
    }

    /* A shorthand that cannot be distributed contributes nothing and invents
     * nothing — the rule the box model's own suite records, and it is deliberately
     * the *shared* rule: five components is not a border width, so no longhand is
     * materialised from it, and the earlier `border-top-width` does not come back
     * either, because the shorthand is later and is therefore the winner. The
     * author's declaration is preserved on the shorthand's own entry, so nothing he
     * wrote is thrown away. */
    {
        CssComputedStyle *five = NULL;
        CssStylesheetId sheetFive = CssDomCascadeSheet(
            &fixture,
            "input { border-top-width: 5px; border-width: 1px 2px 3px 4px 5px }");

        CssDomCompute(&fixture, CssDomRefById(&fixture.dom, "DisplayName"), &five);
        CssComputedIs(five, "border-top-width", "<absent>");
        CssComputedIs(five, "border-width", "1px 2px 3px 4px 5px");
        CssComputedFlagsAre(five, "border-width", "declared");
        CssComputedStyleDestroy(five);
        RoboCheckEqInt(CssStylesheetDestroy(sheetFive, NULL), RoboOk);
    }

    CssComputedStyleDestroy(paragraph);
    CssComputedStyleDestroy(button);
    CssComputedStyleDestroy(heading);
    CssComputedStyleDestroy(bodyStyle);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssDomCascadeEnd(&fixture);
}

void TestRegisterCssDomSelect(void);

void TestRegisterCssDomSelect(void)
{
    TestRegister("css.dom.checkpoint_tree", TestCssDomCheckpointTree);
    TestRegister("css.dom.checkpoint_descendant_and_child",
                       TestCssDomCheckpointDescendantAndChild);
    TestRegister("css.dom.checkpoint_siblings_ignore_text_nodes",
                       TestCssDomCheckpointSiblingsIgnoreTextNodes);
    TestRegister("css.dom.checkpoint_attributes_and_forms",
                       TestCssDomCheckpointAttributesAndForms);
    TestRegister("css.dom.checkpoint_structural",
                       TestCssDomCheckpointStructural);
    TestRegister("css.dom.checkpoint_reports_unevaluatable",
                       TestCssDomCheckpointReportsUnevaluatable);
    TestRegister("css.dom.checkpoint_matching_does_not_mutate",
                       TestCssDomCheckpointMatchingDoesNotMutate);
    TestRegister("css.dom.text_and_comments_are_not_siblings",
                       TestCssDomTextAndCommentsAreNotSiblings);
    TestRegister("css.dom.deep_tree", TestCssDomDeepTree);
    TestRegister("css.dom.rebuild_after_mutation",
                       TestCssDomRebuildAfterMutation);
    TestRegister("css.dom.matching_is_deterministic",
                       TestCssDomMatchingIsDeterministic);
    TestRegister("css.dom.selectors_do_not_disturb_the_picture",
                       TestCssDomSelectorsDoNotDisturbThePicture);
    TestRegister("css.dom.cascade.inline_attribute_comes_from_the_dom",
                       TestCssDomCascadeInlineAttributeComesFromTheDom);
    TestRegister("css.dom.cascade.uses_the_real_tree_relations",
                       TestCssDomCascadeUsesTheRealTreeRelations);
    TestRegister("css.dom.cascade.importance_outranks_inline",
                       TestCssDomCascadeImportanceOutranksInline);
    TestRegister("css.dom.cascade.does_not_disturb_the_picture",
                       TestCssDomCascadeDoesNotDisturbThePicture);
    TestRegister("css.dom.specificity.picks_the_winner",
                       TestCssDomSpecificityPicksTheWinner);
    TestRegister("css.dom.computed.pipeline", TestCssDomComputedPipeline);
    TestRegister("css.dom.computed.does_not_disturb_the_picture",
                       TestCssDomComputedDoesNotDisturbThePicture);
    TestRegister("css.dom.unit.pipeline", TestCssDomUnitPipeline);
    TestRegister("css.dom.box.pipeline", TestCssDomBoxPipeline);
    TestRegister("css.dom.display.pipeline", TestCssDomDisplayPipeline);
    TestRegister("css.dom.position.pipeline", TestCssDomPositionPipeline);
    TestRegister("css.dom.background.pipeline",
                       TestCssDomBackgroundPipeline);
    TestRegister("css.dom.border.pipeline", TestCssDomBorderPipeline);
}
