/*
 * Shared RoboLayout test fixtures (Phase 10.1).
 *
 * Every suite in layout/tests includes this. It is not a framework -- the runtime
 * has exactly one of those (Tests/Framework/RoboTest.h) and this phase adds none,
 * on purpose. It holds the four things a suite would otherwise copy:
 *
 *   fixtures   bring RoboBase, RoboHTML, RoboCSS and RoboLayout up, and build a
 *              real document from a literal
 *   a fake     a style source keyed by tag name, so that the *structural* suites
 *              can prove "a display: none element generates no box" without
 *              standing up a style context, a property table, a registry and a
 *              sheet
 *   a real one a style context with the standard property registry and an author
 *              sheet, for the suites that drive the DOM bridge
 *   helpers    one expression for "how many blocks does the allocator have live",
 *              which is how a leak is caught rather than suspected
 *
 * The CSS half is *shared with css/tests on purpose*. RoboCssTestSupport.h already
 * owns the sheet-parsing fixture and the fixture bring-up, and a second copy here
 * would be a second thing to keep in step with a subsystem this module merely
 * consumes -- the same reasoning the 9.4 DOM suite records for sharing it.
 */

#ifndef ROBO_LAYOUT_TEST_SUPPORT_H
#define ROBO_LAYOUT_TEST_SUPPORT_H

#include "RoboBase/RoboBase.h"
#include "RoboCSS/RoboCSS.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboHTML/RoboHTMLTypes.h"
#include "RoboLayout/RoboLayout.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

/* The CSS fixture helper (sheet parsing and the CSS-side bring-up), shared rather
 * than copied -- see the header comment. */
#include "RoboCssTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Bring-up                                                           */
/* ------------------------------------------------------------------ */

/** Bring every layer a layout test needs up (once) and leave them up. */
static inline void LayoutFixture(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
    if (!CssIsInitialized()) {
        RoboCheckEqInt(CssInit(), RoboOk);
    }
    if (!LayoutIsInitialized()) {
        RoboCheckEqInt(LayoutInit(), RoboOk);
    }
}

/** Live allocator blocks: the number a suite asserts returns to its baseline. */
static inline size_t LayoutLiveBlocks(void)
{
    return MemDebugLiveCount();
}

/** The maximum number of attributes any suite fixture sets. */
#define LAYOUT_TEST_MAX_ATTRIBUTES 8u

/**
 * A real document built from @p html by the real tree builder.
 *
 * `LayoutDocumentBuild` leaves the document **finalized**, and that is a fact a
 * suite has to plan around rather than a detail of this helper. Every path that
 * builds a tree ends at `HtmlTreeBuilderFinish`, which closes tree
 * construction; and `HtmlElementCreate` -- the one door an element comes
 * into existence through -- writes through that gate, so it is refused from the
 * moment the build is done. (The one-shot `HtmlBuildTree` is no different:
 * it is begin + drive-to-EOF + finish.)
 *
 * A suite that needs an element to appear *after* its tree was built therefore
 * cannot parse one, and `LayoutDocumentCreateEmpty` +
 * `LayoutDocumentAddElement` below exist for exactly that case: a document
 * assembled through the DOM seam is never finalized, so it can keep growing while
 * the tree built from it is being tested. A suite that only reads a parsed document
 * wants `LayoutDocumentBuild` and should not care either way.
 */
typedef struct layoutDocument {
    HtmlDocumentId id;
} LayoutDocument;

/* `LayoutDiag diag` appears in these helpers and is passed to RoboHTML
 * doors that take a `HtmlDiag *`. The two records are different types on
 * purpose -- see RoboLayoutDiagnostics.h -- so the helpers below pass NULL to the
 * DOM and keep the failure as a result code, which is the fact a suite asserts. */

static inline void LayoutDocumentBuild(LayoutDocument *doc, const char *html)
{
    HtmlSource source;

    LayoutFixture();
    doc->id = RoboHtmlDocumentIdNone;
    RoboCheckEqInt(HtmlDocumentCreate(&doc->id, NULL), RoboOk);
    /* The source *borrows* @p html and owns nothing, so there is nothing to
     * release -- which is also why the fixture's markup must outlive the tree
     * builder's call, and why every fixture below passes a literal or a buffer
     * that outlives it. */
    HtmlSourceInit(&source);
    RoboCheckEqInt(HtmlSourceString(&source, html, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(doc->id, &source, NULL, NULL), RoboOk);
}

/**
 * Create @p doc with its root node and nothing else -- no source, no parse.
 *
 * The starting point for a document assembled through the DOM seam
 * (RoboHTMLElement.h), which is the only kind of document that can still grow
 * after a layout tree has been built from it (see the note above). Pair it with
 * `LayoutDocumentAddElement` and release it the same way: a document is a
 * document whatever built it.
 */
static inline void LayoutDocumentCreateEmpty(LayoutDocument *doc)
{
    LayoutFixture();
    doc->id = RoboHtmlDocumentIdNone;
    RoboCheckEqInt(HtmlDocumentCreate(&doc->id, NULL), RoboOk);
}

/**
 * Create an element named @p name as the last child of @p parent.
 *
 * The parent is a handle rather than an id, because that is what the DOM's create
 * door takes -- and the document's own root node is one, which is how `<html>`
 * gets its parent. Asserts like the rest of the helpers here: a suite that has to
 * check every element came out well formed would drown its own subject.
 */
static inline HtmlNodeHandle LayoutDocumentAddElement(
    const LayoutDocument *doc, const HtmlNodeHandle *parent, const char *name)
{
    HtmlElementCreateInfo info;
    HtmlNodeHandle out;

    HtmlElementCreateInfoInit(&info);
    info.parent = *parent;
    info.origin = RoboHtmlNodeOriginTest;
    info.namespaceId = RoboHtmlNamespaceHtml;
    info.localName = name;
    HtmlNodeHandleClear(&out);
    RoboCheckEqInt(HtmlElementCreate(doc->id, &info, &out, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&out), 1);
    return out;
}

static inline void LayoutDocumentRelease(LayoutDocument *doc)
{
    if (HtmlDocumentIsLive(doc->id)) {
        RoboCheckEqInt(HtmlDocumentDestroy(doc->id, NULL), RoboOk);
    }
    doc->id = RoboHtmlDocumentIdNone;
}

/**
 * The layout root's element: the first *element* child of the document node.
 *
 * Deliberately the same walk the builder performs rather than a search by tag
 * name, so a fixture whose root element is not `<html>` (a fragment, a bare
 * `<div>`) resolves the same way the builder would resolve it. A name-based
 * helper here would agree with the builder on every document this suite writes
 * and disagree on the one document that matters, which is the one where the
 * suite would be wrong rather than the builder.
 */
static inline HtmlNodeHandle LayoutDocumentElement(const LayoutDocument *doc)
{
    HtmlNodeHandle document = HtmlDocumentRoot(doc->id);
    HtmlNodeView documentView;
    HtmlNodeHandle node;

    HtmlNodeHandleClear(&node);
    if (HtmlNodeGetView(&document, &documentView, NULL) != RoboOk) {
        return node;
    }
    /* The document node is not an element, so the walk starts at its first
     * *child* -- the document element -- exactly as the builder's does. */
    node = documentView.firstChild;
    while (HtmlNodeHandleIsWellformed(&node)) {
        HtmlNodeView view;

        if (HtmlNodeGetView(&node, &view, NULL) != RoboOk) {
            break;
        }
        if (view.kind == RoboHtmlNodeKindElement) {
            return node;
        }
        node = view.NextSibling;
    }
    HtmlNodeHandleClear(&node);
    return node;
}

/** Counts elements, for the walk below. */
typedef struct layoutElementCounter {
    size_t elements;
    uint32_t depthMax;
} LayoutElementCounter;

static inline Result LayoutCountElement(const HtmlNodeView *view,
                                                 size_t depth,
                                                 void *user)
{
    LayoutElementCounter *counter = (LayoutElementCounter *)user;

    if (view && view->kind == RoboHtmlNodeKindElement) {
        counter->elements++;
        if ((uint32_t)depth > counter->depthMax) {
            counter->depthMax = (uint32_t)depth;
        }
    }
    return RoboOk;
}

/**
 * How many elements the *document* has, by the DOM's own walk.
 *
 * The number a build's `elementsSeen` must equal, and the reason these suites
 * ask rather than hardcode: RoboHTML's tree builder inserts the elements a
 * well-formed document requires (an implicit `<head>`, and `<html>`/`<body>`
 * around a bare fragment), so the element count of a fixture is a fact about the
 * *parser*. A suite that hardcoded its own guess would test its guess; one that
 * hardcodes the number *and* says which elements it is counting tests the parser
 * and the builder against each other, which is what is wanted.
 */
/**
 * The deepest box in @p tree, by walking it -- the number a build's `depthMax`
 * must equal and a pass's `depthMax` must equal.
 *
 * Separate from the report on purpose: comparing the report against a walk of the
 * tree is a check that the builder and the tree agree, where comparing it against
 * a number written in the test only checks the number.
 */
static inline uint32_t LayoutTreeMaxDepth(const LayoutTree *tree)
{
    uint32_t deepest = 0u;
    size_t index;

    for (index = 0u; index < LayoutTreeObjectCount(tree); index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;
        uint32_t depth = 0u;

        if (LayoutObjectIdAt(tree, index, &id) != RoboOk) {
            break;
        }
        if (LayoutObjectDepth(tree, id, &depth) != RoboOk) {
            break;
        }
        if (depth > deepest) {
            deepest = depth;
        }
    }
    return deepest;
}

static inline size_t LayoutDocumentElementCount(LayoutDocument *doc)
{
    HtmlNodeHandle root = HtmlDocumentRoot(doc->id);
    LayoutElementCounter counter;

    counter.elements = 0u;
    counter.depthMax = 0u;
    if (!HtmlNodeHandleIsWellformed(&root)) {
        return 0u;
    }
    if (HtmlNodeWalkPreOrder(&root, RoboHtmlNodeFilterElements,
                                      LayoutCountElement, &counter, NULL) != RoboOk) {
        return 0u;
    }
    return counter.elements;
}

/**
 * The element with @p tag anywhere in @p doc, or an unformed handle.
 *
 * The suites that need one specific element ("the `.hidden` div") out of a
 * fixture need this; a suite that asserts *structure* should walk the built tree
 * instead, because the tree is what is under test.
 */
static inline HtmlNodeHandle LayoutDocumentFind(const LayoutDocument *doc,
                                                           const char *tag)
{
    HtmlNodeHandle root = HtmlDocumentRoot(doc->id);
    HtmlNodeHandle found;

    HtmlNodeHandleClear(&found);
    if (!HtmlNodeHandleIsWellformed(&root)) {
        return found;
    }
    if (HtmlNodeFindElement(&root, tag, &found, NULL) != RoboOk) {
        HtmlNodeHandleClear(&found);
    }
    return found;
}

/* ------------------------------------------------------------------ */
/* A fake style source                                                */
/* ------------------------------------------------------------------ */

/** One scripted answer: an element with this tag name computes to this display. */
#define LAYOUT_FAKE_MAX_RULES 8u

typedef struct layoutFakeStyle {
    char tag[16];
    CssComputedStyle *style; /* owned */
} LayoutFakeStyle;

typedef struct layoutFakeSource {
    LayoutFakeStyle rules[LAYOUT_FAKE_MAX_RULES];
    size_t count;
    size_t answers;
} LayoutFakeSource;

static inline const CssComputedStyle *LayoutFakeLookup(void *userdata,
                                                                  const HtmlNodeHandle *element)
{
    LayoutFakeSource *source = (LayoutFakeSource *)userdata;
    const char *name;
    size_t i;

    if (!source || !element) {
        return NULL;
    }
    name = HtmlElementName(element, NULL);
    if (!name || name[0] == '\0') {
        return NULL;
    }
    for (i = 0u; i < source->count; i++) {
        if (strcmp(source->rules[i].tag, name) == 0) {
            source->answers++;
            return source->rules[i].style;
        }
    }
    return NULL;
}

static inline void LayoutFakeInit(LayoutFakeSource *source)
{
    memset(source, 0, sizeof(*source));
}

static inline void LayoutFakeDispose(LayoutFakeSource *source)
{
    size_t i;

    for (i = 0u; i < source->count; i++) {
        CssComputedStyleDestroy(source->rules[i].style);
        source->rules[i].style = NULL;
    }
    source->count = 0u;
    source->answers = 0u;
}

/**
 * Answer for every @p tag with @p display.
 *
 * A *real* computed style holding a real DISPLAY value, built through 9.8's own
 * maker and container rather than through a hand-written struct: a fake that
 * fabricated the value would prove the builder reads a struct, not that it reads
 * a CSS computed value.
 */
static inline void LayoutFakeAdd(LayoutFakeSource *source,
                                   const char *tag,
                                   CssDisplay display)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_FAKE_MAX_RULES, 1);
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeDisplay(&value, display, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("display"),
                                                       0u, &specified, &value, NULL),
                      RoboOk);
    strncpy(source->rules[source->count].tag, tag,
            sizeof(source->rules[source->count].tag) - 1u);
    source->rules[source->count].tag[sizeof(source->rules[source->count].tag) - 1u] = '\0';
    source->rules[source->count].style = style;
    source->count++;
}

/** A source struct pointing at @p source. The struct is a copy by value. */
static inline LayoutStyleSource LayoutFakeSourceOf(LayoutFakeSource *source)
{
    LayoutStyleSource styles;

    LayoutStyleSourceInit(&styles);
    styles.userdata = source;
    styles.ComputedStyleFor = LayoutFakeLookup;
    return styles;
}

/* ------------------------------------------------------------------ */
/* A real CSS context                                                 */
/* ------------------------------------------------------------------ */

/** Everything the DOM bridge needs from RoboCSS, and nothing it owns. */
typedef struct layoutCssFixture {
    CssStyleContext *context;
    CssPropertyTable *table;
    CssPropertyRegistry *registry;
    CssStylesheetId sheet;
} LayoutCssFixture;

static inline void LayoutCssBegin(LayoutCssFixture *fixture)
{
    fixture->context = NULL;
    fixture->table = NULL;
    fixture->registry = NULL;
    fixture->sheet = RoboCssStylesheetIdNone;

    LayoutFixture();
    RoboCheckEqInt(CssStyleContextCreate(&fixture->context, NULL), RoboOk);
    /* The element host is installed by the bridge, which is the point of the
     * bridge: a caller that had to answer four callbacks to use this module would
     * be writing the adapter itself. */
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

/** Parse @p css as an author sheet, parse its selectors, add it to the context. */
static inline void LayoutCssSheet(LayoutCssFixture *fixture, const char *css)
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
    fixture->sheet = id;
}

static inline void LayoutCssEnd(LayoutCssFixture *fixture)
{
    CssStyleContextDestroy(fixture->context);
    fixture->context = NULL;
    CssPropertyRegistryDestroy(fixture->registry);
    fixture->registry = NULL;
    CssPropertyTableDestroy(fixture->table);
    fixture->table = NULL;
    /* The sheet belongs to the module, which is what a stylesheet id means. */
    if (fixture->sheet != RoboCssStylesheetIdNone) {
        RoboCheckEqInt(CssStylesheetDestroy(fixture->sheet, NULL), RoboOk);
        fixture->sheet = RoboCssStylesheetIdNone;
    }
}

#endif /* ROBO_LAYOUT_TEST_SUPPORT_H */
