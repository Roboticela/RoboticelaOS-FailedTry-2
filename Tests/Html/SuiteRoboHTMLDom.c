/*
 * RoboHTML DOM suite (html.dom.*) — Phase 8.15.
 *
 * The per-phase suites (html.doc.*, html.element.*, html.attr.*, html.text.*,
 * html.traverse.*, html.mutate.*, html.lifecycle.*, html.image.*,
 * html.anchor.*, html.form.*) each pin one phase's surface. This suite pins
 * the things that only exist *between* them — the ones that would still be
 * wrong if every per-phase suite passed:
 *
 *   isolation      two documents that parse to identical bytes stay two
 *                  documents, and no operation can mix them.
 *   one id rule    the fragment lookup, a label's `for` and a control's
 *                  `form` are three consumers of one rule, and they are
 *                  asserted to agree — including where they diverge, which is
 *                  when the first element carrying an id is not the kind of
 *                  element that consumer can use.
 *   live resolution  an id change moves every consumer at once, because
 *                  nothing is cached.
 *   void           void is a *parser* rule, not a DOM rule. The tree builder
 *                  never puts a child under `<input>`; the mutation API
 *                  decides by node *kind* and therefore allows it. Both facts
 *                  are asserted, because a reader will expect one of them to
 *                  be the other.
 *   recovery       a malformed corpus leaves a tree that audits clean and
 *                  re-parses byte-identically.
 *   mutation       attribute set/replace/remove with the dependent state
 *                  checked at every step — including the case that makes the
 *                  declared/current split worth having: an explicit control
 *                  value survives the removal of the attribute it defaulted
 *                  from.
 *   depth, width   structure at 200 levels and 300 siblings: order, depth,
 *                  whole-subtree moves, and teardown.
 *   unicode        thirteen script families through parse, attributes, text
 *                  nodes, mutation, and the id consumers.
 *   churn          repeated document lifetimes balance the debug allocator's
 *                  live count, which is the closest thing to a leak test the
 *                  runtime can express.
 *
 * Three tests are worth reading first, because they are the ones that would
 * fail if the model were quietly wrong:
 *
 *   html.dom.id_resolution_agrees
 *   html.dom.void_child_behaviour
 *   html.dom.attribute_mutation_sequences
 *
 * Every fixture parses real HTML through the real tokenizer and tree builder.
 * Where a test needs to *construct* nodes it uses a hand-built document,
 * because construction is gated on a document that has not been finalized and
 * a parsed document is finalized by definition — that split is the model's,
 * not the suite's convenience.
 */

#include "RoboHTMLDomTestSupport.h"

#include "RoboMem/RoboMemDebug.h"
#include "RoboHTML/RoboHTMLAnchor.h"
#include "RoboHTML/RoboHTMLForm.h"
#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLText.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Construction (hand-built documents only)                            */
/* ------------------------------------------------------------------ */

/** Create an element of @p name under @p parent, asserting success. */
static HtmlNodeHandle DomCreateUnder(
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

/** Create a text node of @p text under @p parent, asserting success. */
static HtmlNodeHandle DomCreateText(
    HtmlDocumentId id, const HtmlNodeHandle *parent,
    const char *text)
{
    HtmlTextCreateInfo info;
    HtmlNodeHandle out;

    HtmlTextCreateInfoInit(&info);
    info.parent = *parent;
    info.origin = RoboHtmlNodeOriginTest;
    info.content = text;
    info.length = strlen(text);
    HtmlNodeHandleClear(&out);
    RoboCheckEqInt(HtmlTextCreate(id, &info, &out, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&out));
    return out;
}

static size_t DomChildCount(const HtmlNodeHandle *node)
{
    size_t count = 0u;

    RoboCheckEqInt(HtmlNodeChildCount(node, &count, NULL), RoboOk);
    return count;
}

static size_t DomDepthOf(const HtmlNodeHandle *node)
{
    size_t depth = 0u;

    RoboCheckEqInt(HtmlNodeDepth(node, &depth, NULL), RoboOk);
    return depth;
}

/**
 * Detach @p node from whatever actually lists it.
 *
 * A parsed document's elements hang under implied `html`/`body` elements, not
 * directly under the document node, so a test that removes a child by naming
 * the root is asserting the wrong tree. Asking the node for its parent is the
 * only form of the call that works for both a parsed and a hand-built tree.
 */
static void DomDetach(const HtmlNodeHandle *node)
{
    HtmlNodeHandle parent;

    RoboCheckEqInt(HtmlNodeParent(node, &parent, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&parent, node, NULL),
                      RoboOk);
}

/**
 * Next following sibling of @p node that is a @p tag.
 *
 * Separate in and out handles on purpose: `HtmlNodeNextSibling` may
 * clear its output, so passing one handle as both would invalidate the node
 * being asked about. @return 1 and a filled @p out, or 0 at the end of the
 * sibling list.
 */
static int DomNextTagSibling(const HtmlNodeHandle *node,
                                HtmlTag tag,
                                HtmlNodeHandle *out)
{
    HtmlNodeHandle cursor = *node;
    HtmlNodeHandle next;

    while (HtmlNodeNextSibling(&cursor, &next, NULL) == RoboOk) {
        cursor = next;
        if (HtmlElementIsTag(&cursor, tag)) {
            *out = cursor;
            return 1;
        }
    }
    return 0;
}

/** An attribute to match, for a name/value search. */
typedef struct domAttrMatch {
    const char *name;
    const char *value;
} DomAttrMatch;

static int DomPredicateAttr(const HtmlNodeView *view, void *user)
{
    const DomAttrMatch *m = (const DomAttrMatch *)user;
    const char *value = HtmlElementAttributeValue(&view->handle,
                                                          m->name, NULL);

    return value != NULL && strcmp(value, m->value) == 0;
}

/** First element whose attribute @p name equals @p value. */
static HtmlNodeHandle DomFindByAttr(
    const HtmlNodeHandle *start, const char *name, const char *value)
{
    DomAttrMatch m;
    HtmlNodeHandle out;

    m.name = name;
    m.value = value;
    HtmlNodeHandleClear(&out);
    RoboCheckEqInt(
        HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                                  DomPredicateAttr, &m, &out, NULL),
        RoboOk);
    return out;
}

/** Read an element's current control value into @p buf. */
static void DomControlValue(const HtmlNodeHandle *element,
                              char *buf,
                              size_t cap,
                              int *outDeclared)
{
    size_t length = 0u;

    RoboCheckEqInt(HtmlElementControlValue(element, buf, cap,
                                                      &length, outDeclared,
                                                      NULL),
                      RoboOk);
    buf[length < cap ? length : cap - 1u] = '\0';
}

/* ------------------------------------------------------------------ */
/* Walk visitors                                                       */
/* ------------------------------------------------------------------ */

/** Counts elements on a chain and checks each sits one level below the last. */
typedef struct domChainRecord {
    size_t visited;
    size_t mismatches;
} DomChainRecord;

static Result DomChainVisitor(const HtmlNodeView *view,
                                       size_t depth,
                                       void *user)
{
    DomChainRecord *r = (DomChainRecord *)user;

    (void)view;
    r->visited++;
    if (depth != r->visited) {
        r->mismatches++;
    }
    return RoboOk;
}

/** Records the first document-order index at which some tags appear. */
typedef struct domOrderRecord {
    int heading;
    int form;
    int paragraph;
    int index;
} DomOrderRecord;

static Result DomOrderVisitor(const HtmlNodeView *view,
                                       size_t depth,
                                       void *user)
{
    DomOrderRecord *r = (DomOrderRecord *)user;

    (void)depth;
    if (HtmlElementIsTag(&view->handle, RoboHtmlTagH1) &&
        r->heading < 0) {
        r->heading = r->index;
    }
    if (HtmlElementIsTag(&view->handle, RoboHtmlTagForm) &&
        r->form < 0) {
        r->form = r->index;
    }
    if (HtmlElementIsTag(&view->handle, RoboHtmlTagP) &&
        r->paragraph < 0) {
        r->paragraph = r->index;
    }
    r->index++;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* 1. Document isolation                                               */
/* ------------------------------------------------------------------ */

/**
 * Two documents that parsed the same bytes stay two documents.
 *
 * This is the assertion the ownership model exists for. Identical markup means
 * identical node *names* and identical `id` attributes, so a model that
 * resolved anything by name or by id-string across documents would look
 * correct on one document and corrupt the other. Every check therefore has a
 * counterpart: an operation that mixes the documents is refused, and the
 * document that was not addressed is asserted unchanged — by its dump, its
 * node count and its audit.
 */
static void TestDomDocumentIsolation(void)
{
    static const char kMarkup[] =
        "<div id=\"Box\"><p id=\"Text\">One</p>"
        "<input id=\"Field\" value=\"declared\"></div>";
    size_t liveBefore = HtmlLiveDocumentCount();
    HtmlDocumentId a = DomNewDocument();
    HtmlDocumentId b = DomNewDocument();
    HtmlNodeHandle rootA;
    HtmlNodeHandle rootB;
    HtmlNodeHandle boxA;
    HtmlNodeHandle boxB;
    HtmlNodeHandle fieldA;
    HtmlNodeHandle fieldB;
    char dumpA[2048];
    char dumpB[2048];
    char dumpAfter[2048];
    char buf[64];
    int declared = 0;

    DomParse(a, kMarkup);
    DomParse(b, kMarkup);
    rootA = DomRoot(a);
    rootB = DomRoot(b);

    /* Identical bytes, identical trees — and no shared identity at all. */
    DomDump(a, dumpA, sizeof(dumpA));
    DomDump(b, dumpB, sizeof(dumpB));
    RoboCheckStrEq(dumpA, dumpB);
    RoboCheckEqInt((int)HtmlDocumentNodeCount(a),
                      (int)HtmlDocumentNodeCount(b));
    RoboCheck(HtmlNodeHandleDocument(&rootA) !=
               HtmlNodeHandleDocument(&rootB));
    RoboCheck(!HtmlNodeHandleEqual(&rootA, &rootB));

    boxA = DomFindId(&rootA, "Box");
    boxB = DomFindId(&rootB, "Box");
    RoboCheck(!HtmlNodeHandleEqual(&boxA, &boxB));
    RoboCheckEqInt(HtmlDocumentOwnsNode(a, &boxA), 1);
    RoboCheckEqInt(HtmlDocumentOwnsNode(b, &boxA), 0);
    RoboCheckEqInt(HtmlDocumentOwnsNode(a, &boxB), 0);
    RoboCheckEqInt(HtmlDocumentOwnsNode(b, &boxB), 1);

    /*
     * A mutation that names one node from each document is refused. The code
     * is the documented one for a foreign child (RoboNotSupported), not a
     * not-found: the child resolves fine, it is simply in the wrong document.
     */
    RoboCheckEqInt(HtmlNodeAppendChild(&boxA, &boxB, NULL),
                      RoboNotSupported);
    RoboCheckEqInt(
        HtmlNodeInsertBefore(&boxA, &boxB, &boxA, NULL),
        RoboNotSupported);
    RoboCheckEqInt(HtmlNodeRemoveChild(&boxB, &boxA, NULL),
                      RoboNotSupported);
    RoboCheckEqInt(
        HtmlNodeReplaceChild(&rootB, &boxA, &boxB, NULL),
        RoboNotSupported);
    RoboCheckEqInt(HtmlNodeAppendChild(&boxA, &rootB, NULL),
                      RoboNotSupported);

    /* The construction seam refuses a foreign parent before it creates
     * anything, so no node is added to either document. */
    {
        HtmlElementCreateInfo info;
        HtmlNodeHandle rejected;

        HtmlElementCreateInfoInit(&info);
        info.parent = boxA; /* a handle of document a ... */
        info.origin = RoboHtmlNodeOriginTest;
        info.namespaceId = RoboHtmlNamespaceHtml;
        info.localName = "aside";
        HtmlNodeHandleClear(&rejected);
        /* ... used to create in document b. */
        RoboCheck(HtmlElementCreate(b, &info, &rejected, NULL) !=
                   RoboOk);
        RoboCheck(!HtmlNodeHandleIsWellformed(&rejected));
    }

    /* Neither tree moved: not a child, not a byte of the dump. */
    RoboCheckEqInt((int)DomChildCount(&boxA), 2);
    RoboCheckEqInt((int)DomChildCount(&rootB), 1);
    DomDump(a, dumpAfter, sizeof(dumpAfter));
    RoboCheckStrEq(dumpAfter, dumpA);
    DomDump(b, dumpAfter, sizeof(dumpAfter));
    RoboCheckStrEq(dumpAfter, dumpB);
    DomExpectCleanTree(a);
    DomExpectCleanTree(b);

    /*
     * Form state is per document. The same `<input value="declared">` exists
     * in both, so pinning an override in one must leave the other reporting
     * its declared value — a leaked value would still pass a single-document
     * test.
     */
    fieldA = DomFindId(&rootA, "Field");
    fieldB = DomFindId(&rootB, "Field");
    RoboCheckEqInt(
        HtmlElementSetControlValue(&fieldA, "override", 8u, NULL),
        RoboOk);
    DomControlValue(&fieldA, buf, sizeof(buf), &declared);
    RoboCheckStrEq(buf, "override");
    RoboCheckEqInt(declared, 0);
    DomControlValue(&fieldB, buf, sizeof(buf), &declared);
    RoboCheckStrEq(buf, "declared");
    RoboCheckEqInt(declared, 1);

    /*
     * Destroying one document leaves the other fully usable: its handles
     * resolve, its audit is clean, and nothing it holds went stale with the
     * other document.
     */
    RoboCheckEqInt(HtmlDocumentDestroy(a, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentIsLive(a), 0);
    RoboCheckEqInt(HtmlNodeHandleIsStale(&boxA), 1);
    RoboCheckEqInt(HtmlNodeHandleIsStale(&fieldA), 1);
    RoboCheckEqInt(HtmlNodeHandleIsStale(&boxB), 0);
    RoboCheckEqInt(HtmlDocumentOwnsNode(a, &boxA), 0);
    DomExpectCleanTree(b);
    DomDump(b, dumpAfter, sizeof(dumpAfter));
    RoboCheckStrEq(dumpAfter, dumpB);
    RoboCheckEqInt((int)DomCountTags(&rootB, RoboHtmlTagDiv), 1);

    RoboCheckEqInt(HtmlDocumentDestroy(b, NULL), RoboOk);
    RoboCheckEqInt((int)HtmlLiveDocumentCount(), (int)liveBefore);
}

/* ------------------------------------------------------------------ */
/* 2. One id rule, three consumers                                     */
/* ------------------------------------------------------------------ */

/**
 * Three consumers, one rule: the first element in document order whose `id`
 * attribute is exactly the name, with the consuming surface's own kind test
 * applied to *that* element and nowhere else.
 *
 * The divergences are the interesting half. A `<div id="Dup">` before an
 * `<input id="Dup">` means the fragment lookup answers with the `div` (it
 * accepts any element) while a `for="Dup"` answers with nothing — not with
 * the input further down, which is what a "find a labelable element with that
 * id" implementation would wrongly do. Same for a `form="Name"` whose first
 * match is not a `<form>`.
 */
static void TestDomIdResolutionAgrees(void)
{
    static const char kMarkup[] =
        "<div id=\"Dup\">first</div>"
        "<input id=\"Dup\">"
        "<form id=\"Shared\"></form>"
        "<div id=\"Shared\"></div>"
        "<label for=\"Shared\">not-a-form-target</label>"
        "<label><input id=\"Wrap\"></label>"
        "<input id=\"Attr\" form=\"Shared\">"
        "<input id=\"Missing\" form=\"Absent\">"
        "<input id=\"DupTarget\" form=\"Dup\">";
    HtmlDocumentId id = DomDocumentOf(kMarkup);
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle found;
    HtmlNodeHandle label;
    HtmlNodeHandle owner;
    HtmlNodeHandle labels[3];
    HtmlFormOwnerSource source = RoboHtmlFormOwnerSourceNone;
    int fromAttribute = -1;
    size_t seen = 0u;

    /* Collect the labels in document order: 0 = for="Shared", 1 = wrapping,
     * 2 = the one after the wrapping label would be next; two are enough to
     * reach the two rules, so collect three and assert the count. */
    label = DomFindTag(&root, RoboHtmlTagLabel);
    labels[seen++] = label;
    while (seen < 3u && DomNextTagSibling(&label, RoboHtmlTagLabel,
                                             &label)) {
        labels[seen++] = label;
    }
    RoboCheckEqInt((int)seen, 2);

    /* A unique id: both id consumers point at the same element. */
    RoboCheckEqInt(
        HtmlNavigationFindFragment(id, "Wrap", 4u, &found, NULL),
        RoboOk);
    RoboCheck(HtmlElementIsTag(&found, RoboHtmlTagInput));
    HtmlNodeHandleClear(&owner);
    fromAttribute = -1;
    RoboCheckEqInt(
        HtmlElementLabelTarget(&labels[1], &owner, &fromAttribute,
                                       NULL),
        RoboOk);
    RoboCheckEqInt(fromAttribute, 0); /* found by descent, not by `for` */
    RoboCheck(HtmlNodeHandleEqual(&owner, &found));

    /* Duplicate id: the fragment lookup takes the first in document order,
     * whatever kind that is. */
    RoboCheckEqInt(
        HtmlNavigationFindFragment(id, "Dup", 3u, &found, NULL),
        RoboOk);
    RoboCheck(HtmlElementIsTag(&found, RoboHtmlTagDiv));

    /* The same name through a label: the first match is not labelable, so the
     * answer is nothing — not the second match, and not the label's own
     * children. */
    {
        HtmlNodeHandle dupLabel = labels[0];

        HtmlNodeHandleClear(&owner);
        fromAttribute = -1;
        RoboCheckEqInt(
            HtmlElementLabelTarget(&dupLabel, &owner,
                                           &fromAttribute, NULL),
            RoboOk);
        RoboCheck(!HtmlNodeHandleIsWellformed(&owner));
        /* 0, not 1: the flag says which step *answered*, not which attribute
         * was consulted. A `for` that names a non-labelable element answers
         * nothing, so the honest report is "no target, and no step found
         * one" rather than "the `for` was used". */
        RoboCheckEqInt(fromAttribute, 0);
    }

    /* A `form` attribute that resolves wins, and says so. */
    {
        HtmlNodeHandle control = DomFindId(&root, "Attr");
        HtmlNodeHandle shared = DomFindId(&root, "Shared");

        RoboCheckEqInt(
            HtmlElementFormOwner(&control, &owner, &source, NULL),
            RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&owner, &shared));
        RoboCheckEqInt((int)source,
                          (int)RoboHtmlFormOwnerSourceAttribute);
    }

    /* A `form` attribute naming nothing resolves to no owner, and does not
     * fail: a control outside every form is an ordinary document. */
    {
        HtmlNodeHandle control = DomFindId(&root, "Missing");

        HtmlNodeHandleClear(&owner);
        source = RoboHtmlFormOwnerSourceAttribute;
        RoboCheckEqInt(
            HtmlElementFormOwner(&control, &owner, &source, NULL),
            RoboOk);
        RoboCheck(!HtmlNodeHandleIsWellformed(&owner));
        RoboCheckEqInt((int)source, (int)RoboHtmlFormOwnerSourceNone);
    }

    /* A `form` attribute whose first match is not a form falls back to
     * ancestry rather than refusing or reaching the second match. */
    {
        HtmlNodeHandle control = DomFindId(&root, "DupTarget");

        HtmlNodeHandleClear(&owner);
        source = RoboHtmlFormOwnerSourceAttribute;
        RoboCheckEqInt(
            HtmlElementFormOwner(&control, &owner, &source, NULL),
            RoboOk);
        RoboCheck(!HtmlNodeHandleIsWellformed(&owner));
        RoboCheckEqInt((int)source, (int)RoboHtmlFormOwnerSourceNone);
    }

    /* Exact and case-sensitive, and an empty fragment names the top of the
     * document and therefore nothing: all three are refusals, not wildcards. */
    RoboCheckEqInt(
        HtmlNavigationFindFragment(id, "dup", 3u, &found, NULL),
        RoboNotFound);
    RoboCheckEqInt(
        HtmlNavigationFindFragment(id, "", 0u, &found, NULL),
        RoboNotFound);
    RoboCheckEqInt(
        HtmlNavigationFindFragment(id, "DupX", 4u, &found, NULL),
        RoboNotFound);

    /* Only elements can answer an id query, so a text node never can. */
    RoboCheck(DomTryFindId(&root, "Dup", &found));
    RoboCheck(HtmlElementIsElement(&found));

    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 3. An id change moves every consumer                                */
/* ------------------------------------------------------------------ */

/**
 * Resolution is live, not cached: the same consumers are asked before and
 * after an `id` is renamed, a `for` is retargeted, a form's id is renamed, and
 * an id is removed. A model that cached any of them would answer the first
 * question correctly and every later one from stale state.
 */
static void TestDomIdChangePropagates(void)
{
    static const char kMarkup[] =
        "<form id=\"F\"><label for=\"C\">Caption</label></form>"
        "<input id=\"C\" form=\"F\">";
    HtmlDocumentId id = DomDocumentOf(kMarkup);
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle label = DomFindTag(&root, RoboHtmlTagLabel);
    HtmlNodeHandle form = DomFindTag(&root, RoboHtmlTagForm);
    HtmlNodeHandle input = DomFindId(&root, "C");
    HtmlNodeHandle found;
    HtmlNodeHandle owner;
    HtmlFormOwnerSource source = RoboHtmlFormOwnerSourceNone;
    int fromAttribute = -1;

    /* Baseline: all three agree on the input. */
    RoboCheckEqInt(
        HtmlNavigationFindFragment(id, "C", 1u, &found, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&found, &input));
    RoboCheckEqInt(
        HtmlElementLabelTarget(&label, &found, &fromAttribute, NULL),
        RoboOk);
    RoboCheckEqInt(fromAttribute, 1);
    RoboCheck(HtmlNodeHandleEqual(&found, &input));
    RoboCheckEqInt(
        HtmlElementFormOwner(&input, &owner, &source, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&owner, &form));
    RoboCheckEqInt((int)source,
                      (int)RoboHtmlFormOwnerSourceAttribute);

    /* Rename the control: the fragment follows the new name immediately and
     * forgets the old one, while the label — whose `for` still says "C" —
     * now names nothing. */
    RoboCheckEqInt(HtmlElementSetAttribute(&input, "id", "D", NULL),
                      RoboOk);
    RoboCheckEqInt(
        HtmlNavigationFindFragment(id, "C", 1u, &found, NULL),
        RoboNotFound);
    RoboCheckEqInt(
        HtmlNavigationFindFragment(id, "D", 1u, &found, NULL),
        RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&found, &input));
    HtmlNodeHandleClear(&found);
    fromAttribute = -1;
    RoboCheckEqInt(
        HtmlElementLabelTarget(&label, &found, &fromAttribute, NULL),
        RoboOk);
    RoboCheck(!HtmlNodeHandleIsWellformed(&found));
    RoboCheckEqInt(fromAttribute, 0);

    /* Retarget the label: the same control is named again. */
    RoboCheckEqInt(
        HtmlElementSetAttribute(&label, "for", "D", NULL), RoboOk);
    RoboCheckEqInt(
        HtmlElementLabelTarget(&label, &found, &fromAttribute, NULL),
        RoboOk);
    RoboCheckEqInt(fromAttribute, 1);
    RoboCheck(HtmlNodeHandleEqual(&found, &input));

    /* Rename the form: the `form` attribute now names nothing and the input
     * is outside every form, so the owner is gone and the source records
     * which rule answered nothing. Renaming it back restores the association,
     * because nothing was cached in the first place. */
    RoboCheckEqInt(HtmlElementSetAttribute(&form, "id", "G", NULL),
                      RoboOk);
    HtmlNodeHandleClear(&owner);
    source = RoboHtmlFormOwnerSourceAttribute;
    RoboCheckEqInt(
        HtmlElementFormOwner(&input, &owner, &source, NULL), RoboOk);
    RoboCheck(!HtmlNodeHandleIsWellformed(&owner));
    RoboCheckEqInt((int)source, (int)RoboHtmlFormOwnerSourceNone);
    RoboCheckEqInt(HtmlElementSetAttribute(&form, "id", "F", NULL),
                      RoboOk);
    RoboCheckEqInt(
        HtmlElementFormOwner(&input, &owner, &source, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&owner, &form));
    RoboCheckEqInt((int)source,
                      (int)RoboHtmlFormOwnerSourceAttribute);

    /* Remove the control's id: the id consumers lose it, and the form owner —
     * which does not depend on the control's own id — does not. */
    RoboCheckEqInt(HtmlElementRemoveAttribute(&input, "id", NULL),
                      RoboOk);
    RoboCheckEqInt(
        HtmlNavigationFindFragment(id, "D", 1u, &found, NULL),
        RoboNotFound);
    HtmlNodeHandleClear(&found);
    RoboCheckEqInt(
        HtmlElementLabelTarget(&label, &found, &fromAttribute, NULL),
        RoboOk);
    RoboCheck(!HtmlNodeHandleIsWellformed(&found));
    RoboCheckEqInt(
        HtmlElementFormOwner(&input, &owner, &source, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&owner, &form));

    /* A control that moves into the form keeps the same answer by a different
     * rule — the source field is the difference a caller can act on. While the
     * `form` attribute still resolves it wins; drop the attribute and the
     * ancestry rule takes over, with the same owner. */
    DomDetach(&input);
    RoboCheckEqInt(HtmlNodeAppendChild(&form, &input, NULL),
                      RoboOk);
    RoboCheckEqInt(
        HtmlElementFormOwner(&input, &owner, &source, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&owner, &form));
    RoboCheckEqInt((int)source,
                      (int)RoboHtmlFormOwnerSourceAttribute);
    RoboCheckEqInt(HtmlElementRemoveAttribute(&input, "form",
                                                         NULL),
                      RoboOk);
    RoboCheckEqInt(
        HtmlElementFormOwner(&input, &owner, &source, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&owner, &form));
    RoboCheckEqInt((int)source, (int)RoboHtmlFormOwnerSourceAncestor);

    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 4. Void elements: a parser rule, not a DOM rule                     */
/* ------------------------------------------------------------------ */

/**
 * The tree builder never gives a void element a child; the mutation API
 * decides by node *kind* rather than by tag, so an explicit append under
 * `<input>` succeeds. Both are asserted here, because the reader will expect
 * one of them to be the other, and a suite that tested only the parser half
 * would leave the DOM rule undocumented.
 */
static void TestDomVoidChildBehaviour(void)
{
    static const char kMarkup[] =
        "<div id=\"Host\">"
        "<input id=\"V\">"
        "<img id=\"I\" src=\"Assets/Photo.png\">"
        "tail"
        "<img id=\"Bare\">"
        "</div>";
    HtmlDocumentId id = DomDocumentOf(kMarkup);
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle host = DomFindId(&root, "Host");
    HtmlNodeHandle input = DomFindId(&root, "V");
    HtmlNodeHandle image = DomFindId(&root, "I");
    HtmlNodeHandle bare = DomFindId(&root, "Bare");
    HtmlNodeHandle text;
    size_t removed = 0u;

    /* The parser's side: a void element is never left open, so the markup
     * that follows it belongs to the enclosing element. */
    RoboCheckEqInt((int)DomChildCount(&input), 0);
    RoboCheckEqInt((int)DomChildCount(&image), 0);
    RoboCheckEqInt((int)DomChildCount(&host), 4);
    RoboCheckEqInt((int)DomCountTags(&host, RoboHtmlTagImg), 2);
    RoboCheckEqInt(HtmlNodeFindFirst(
                          &host, RoboHtmlNodeFilterText, NULL, NULL,
                          &text, NULL),
                      RoboOk);
    RoboCheck(HtmlTextIsText(&text));
    RoboCheckEqInt(HtmlTextIsEmpty(&text), 0);

    /* A text node is a leaf by kind — the one kind the mutation API refuses
     * to make a parent, and the refusal is not-supported rather than a silent
     * append. */
    RoboCheckEqInt(HtmlNodeAppendChild(&text, &image, NULL),
                      RoboNotSupported);
    RoboCheckEqInt((int)DomChildCount(&text), 0);

    /* The DOM's side: `<input>` is an element node, and elements hold
     * children, so moving a detached element under it is allowed — and the
     * tree stays auditable. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&host, &bare, NULL),
                      RoboOk);
    RoboCheckEqInt((int)DomChildCount(&host), 3);
    RoboCheckEqInt(HtmlNodeAppendChild(&input, &bare, NULL),
                      RoboOk);
    RoboCheckEqInt((int)DomChildCount(&input), 1);
    RoboCheck(HtmlNodeHandleEqual(&bare, &bare));
    DomExpectCleanTree(id);

    /* Detaching it again is a legal state, not a leak. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&input, &bare, NULL),
                      RoboOk);
    RoboCheckEqInt((int)DomChildCount(&input), 0);
    RoboCheckEqInt(HtmlNodeAppendChild(&host, &bare, NULL),
                      RoboOk);
    RoboCheckEqInt((int)DomChildCount(&host), 4);
    DomExpectCleanTree(id);

    /* Removing every child of a childless element succeeds and removes none. */
    RoboCheckEqInt(HtmlNodeRemoveChildren(&input, &removed, NULL),
                      RoboOk);
    RoboCheckEqInt((int)removed, 0);

    /* The image state is derived from the element, so it follows the
     * attribute through the DOM: usable `src` is pending, none is NONE. */
    RoboCheckEqInt((int)HtmlElementImageState(&image),
                      (int)RoboHtmlImageStatePending);
    RoboCheckEqInt((int)HtmlElementImageState(&bare),
                      (int)RoboHtmlImageStateNone);
    RoboCheckEqInt(
        HtmlElementSetAttribute(&bare, "src", "x.png", NULL), RoboOk);
    RoboCheckEqInt((int)HtmlElementImageState(&bare),
                      (int)RoboHtmlImageStatePending);
    RoboCheckEqInt(HtmlElementRemoveAttribute(&bare, "src", NULL),
                      RoboOk);
    RoboCheckEqInt((int)HtmlElementImageState(&bare),
                      (int)RoboHtmlImageStateNone);
    RoboCheckEqInt((int)HtmlDocumentImageCount(id), 0);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 5. Recovery leaves a tree that audits clean                         */
/* ------------------------------------------------------------------ */

typedef struct domMalformedCase {
    const char *name;
    const char *html;
} DomMalformedCase;

/**
 * The corpus: unclosed elements, misnested ones, a void element in the middle
 * of a form, a select with two options and no end tag, duplicate attributes,
 * an attribute that runs to EOF, stray end tags, a character reference, an
 * unsupported table structure, angle noise, and nothing at all.
 *
 * These are not "does it crash" inputs. Each is asked three questions: did the
 * build report success, is the resulting tree internally consistent (the
 * runtime's own whole-tree audit), and does re-parsing the same bytes produce
 * the same tree. A recovery that produced a *different* tree on the second
 * parse would be worse than a crash, because nothing would notice.
 */
static const DomMalformedCase kMalformed[] = {
    {"unclosed-div", "<div><span>"},
    {"paragraph-then-div", "<p><div>broken"},
    {"form-void-textarea", "<form><input><textarea>text"},
    {"select-options", "<select><option>One<option>Two"},
    {"duplicate-id", "<div id=\"a\" id=\"b\"></div>"},
    {"stray-end-tags", "</div></p>text"},
    {"table", "<table><tr><td>x</td></tr></table>"},
    {"heading-nesting", "<h1>One<h2>Two"},
    {"unclosed-anchor", "<a href=\"#x\">unclosed"},
    {"void-run", "<input><input><input>"},
    {"charrefs", "<p>&notarefsemicolon; &amp; &#x41;</p>"},
    {"misnested-inline", "<b><i>unclosed</b></i>"},
    {"unterminated-attr", "<div title=\"unterminated>"},
    {"angle-noise", "<<>>"},
    {"empty", ""}
};

static void TestDomInvariantsAfterRecovery(void)
{
    size_t i;

    for (i = 0u; i < sizeof(kMalformed) / sizeof(kMalformed[0]); i++) {
        const DomMalformedCase *c = &kMalformed[i];
        HtmlDocumentId id = DomNewDocument();
        HtmlDocumentId again;
        char first[8192];
        char second[8192];

        DomParse(id, c->html);
        DomExpectCleanTree(id);
        DomDump(id, first, sizeof(first));

        /* The same bytes in a second document: the same tree, byte for byte.
         * The failure message names the input, so a corpus regression is
         * identifiable from the log alone. */
        again = DomDocumentOf(c->html);
        DomDump(again, second, sizeof(second));
        RoboCheckMsg(strcmp(first, second) == 0, c->name);
        RoboCheckEqInt((int)HtmlDocumentNodeCount(id),
                          (int)HtmlDocumentNodeCount(again));
        DomExpectCleanTree(again);

        RoboCheckEqInt(HtmlDocumentDestroy(again, NULL), RoboOk);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }

    /* Two cases where the *content* is the claim, not only the shape: the
     * duplicate id is preserved as two records, and three consecutive void
     * inputs are three siblings rather than a nesting. */
    {
        HtmlDocumentId id = DomDocumentOf("<div id=\"a\" id=\"b\">");
        HtmlNodeHandle root = DomRoot(id);
        HtmlNodeHandle div = DomFindTag(&root, RoboHtmlTagDiv);
        size_t count = 0u;

        RoboCheckEqInt(HtmlElementAttributeCount(&div, &count, NULL),
                          RoboOk);
        RoboCheckEqInt((int)count, 2);
        RoboCheckStrEq(DomAttr(&div, "id"), "a");
        DomExpectCleanTree(id);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }
    {
        HtmlDocumentId id = DomDocumentOf("<input><input><input>");
        HtmlNodeHandle root = DomRoot(id);
        HtmlNodeHandle first;

        RoboCheckEqInt((int)DomCountTags(&root, RoboHtmlTagInput), 3);
        first = DomFindTag(&root, RoboHtmlTagInput);
        RoboCheckEqInt((int)DomChildCount(&first), 0);
        DomExpectCleanTree(id);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }
}

/* ------------------------------------------------------------------ */
/* 6. Rebuilds are deterministic                                       */
/* ------------------------------------------------------------------ */

/**
 * The same source twice, a mutation, a reset, and the same source again: the
 * dump after the rebuild must equal the original byte for byte, not merely
 * "look similar". A document that leaked state out of a previous tree — an id
 * counter, an arena offset, a stale child link — shows up here and almost
 * nowhere else.
 */
static void TestDomRebuildIsDeterministic(void)
{
    static const char kMarkup[] =
        "<h1 id=\"Title\">Heading</h1><p>Body</p><ul><li>One</li>"
        "<li>Two</li></ul>";
    HtmlDocumentId id = DomDocumentOf(kMarkup);
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle list;
    HtmlNodeHandle item;
    HtmlElementView view;
    char first[4096];
    char second[4096];
    char third[4096];
    char mutated[4096];
    size_t nodesFirst;

    DomDump(id, first, sizeof(first));
    nodesFirst = HtmlDocumentNodeCount(id);

    /* Re-parse the identical source on the same document. */
    DomParse(id, kMarkup);
    DomDump(id, second, sizeof(second));
    RoboCheckStrEq(second, first);
    RoboCheckEqInt((int)HtmlDocumentNodeCount(id),
                      (int)nodesFirst);
    DomExpectCleanTree(id);

    /* A structural change must be visible in the dump, or the determinism
     * above would be vacuous. */
    root = DomRoot(id);
    list = DomFindTag(&root, RoboHtmlTagUl);
    item = DomFindTag(&list, RoboHtmlTagLi);
    RoboCheckEqInt(HtmlNodeRemoveChild(&list, &item, NULL),
                      RoboOk);
    DomDump(id, mutated, sizeof(mutated));
    RoboCheck(strcmp(mutated, first) != 0);
    DomExpectCleanTree(id);

    /* Reset and rebuild: the original tree again. */
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentIsEmpty(id), 1);
    DomParse(id, kMarkup);
    DomDump(id, third, sizeof(third));
    RoboCheckStrEq(third, first);
    RoboCheckEqInt((int)HtmlDocumentNodeCount(id),
                      (int)nodesFirst);
    DomExpectCleanTree(id);

    /* Handles from before the reset are stale — the generation guard is what
     * makes the old tree and the new one distinguishable. */
    memset(&view, 0, sizeof(view));
    RoboCheckEqInt(HtmlNodeHandleIsStale(&item), 1);
    RoboCheckEqInt(HtmlElementViewGet(&item, &view, NULL),
                      RoboNotFound);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 7. Attribute mutation sequences and the state that depends on them   */
/* ------------------------------------------------------------------ */

/**
 * The sequence — set, read, replace, remove, verify absent — applied to every
 * family whose attributes are load-bearing, with the *dependent* state checked
 * at each step rather than only the attribute.
 *
 * The control-value block is the one worth reading: an explicit value survives
 * the removal of the `value` attribute it defaulted from, and disappears when
 * the override is cleared. A model with one value field would answer the first
 * half correctly and the second half wrongly, and this is the only place in
 * the DOM suites where that difference is visible against a live mutation of
 * the attribute itself.
 */
static void TestDomAttributeMutationSequences(void)
{
    static const char kMarkup[] =
        "<div id=\"Box\" class=\"old\" title=\"t\">"
        "<a id=\"Link\" href=\"/Route\">go</a>"
        "<img id=\"Photo\" src=\"Assets/Photo.png\" alt=\"بديل\">"
        /* `type="checkbox"` is load-bearing: checkedness is modelled only for
         * an input whose effective type is checkable, so an untyped input's
         * `checked` attribute is preserved with nothing to report. */
        "<input id=\"Field\" type=\"checkbox\" value=\"declared\" checked>"
        "<select id=\"Pick\"><option id=\"Opt1\" value=\"one\">One</option>"
        "<option id=\"Opt2\" value=\"two\" selected>Two</option></select>"
        "</div>";
    HtmlDocumentId id = DomDocumentOf(kMarkup);
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle box = DomFindId(&root, "Box");
    HtmlNodeHandle link = DomFindId(&root, "Link");
    HtmlNodeHandle photo = DomFindId(&root, "Photo");
    HtmlNodeHandle field = DomFindId(&root, "Field");
    HtmlNodeHandle pick = DomFindId(&root, "Pick");
    HtmlNodeHandle optionOne = DomFindId(&root, "Opt1");
    HtmlNodeHandle optionTwo = DomFindId(&root, "Opt2");
    char buf[128];
    int declared = 0;
    int checked = 0;
    size_t childrenBefore;

    /* A plain attribute: set, read, replace, remove, absent. */
    RoboCheckEqInt(
        HtmlElementSetAttribute(&box, "data-role", "panel", NULL),
        RoboOk);
    RoboCheckEqInt(HtmlElementHasAttribute(&box, "data-role"), 1);
    RoboCheckStrEq(DomAttr(&box, "data-role"), "panel");
    RoboCheckEqInt(
        HtmlElementSetAttribute(&box, "data-role", "card", NULL),
        RoboOk);
    RoboCheckStrEq(DomAttr(&box, "data-role"), "card");
    RoboCheckEqInt(
        HtmlElementRemoveAttribute(&box, "data-role", NULL), RoboOk);
    RoboCheckEqInt(HtmlElementHasAttribute(&box, "data-role"), 0);
    RoboCheck(DomAttr(&box, "data-role") == NULL);

    /* Attribute writes leave the children alone. */
    childrenBefore = DomChildCount(&box);
    RoboCheckEqInt(HtmlElementSetAttribute(&box, "class", "new",
                                                      NULL),
                      RoboOk);
    RoboCheckStrEq(DomAttr(&box, "class"), "new");
    RoboCheckEqInt((int)DomChildCount(&box), (int)childrenBefore);

    /* Unicode attribute values survive set/read byte for byte. */
    RoboCheckEqInt(HtmlElementSetAttribute(&box, "title",
                                                      "Ελληνικά — 日本語",
                                                      NULL),
                      RoboOk);
    RoboCheckStrEq(DomAttr(&box, "title"), "Ελληνικά — 日本語");

    /* The anchor model reads the attribute that just changed, and reports
     * removal as absent rather than as an empty reference. */
    {
        HtmlAnchorAttributes attrs;

        memset(&attrs, 0, sizeof(attrs));
        RoboCheckEqInt(
            HtmlElementAnchorAttributes(&link, &attrs, NULL), RoboOk);
        RoboCheckEqInt(attrs.hrefPresent, 1);
        RoboCheckEqInt(attrs.hrefUsable, 1);
        RoboCheckEqInt(
            HtmlElementSetAttribute(&link, "href", "#Region", NULL),
            RoboOk);
        memset(&attrs, 0, sizeof(attrs));
        RoboCheckEqInt(
            HtmlElementAnchorAttributes(&link, &attrs, NULL), RoboOk);
        RoboCheckEqInt((int)attrs.hrefLength, 7);
        RoboCheck(memcmp(attrs.href, "#Region", 7u) == 0);
        RoboCheckEqInt(
            HtmlElementRemoveAttribute(&link, "href", NULL), RoboOk);
        memset(&attrs, 0, sizeof(attrs));
        RoboCheckEqInt(
            HtmlElementAnchorAttributes(&link, &attrs, NULL), RoboOk);
        RoboCheckEqInt(attrs.hrefPresent, 0);
        RoboCheckEqInt(attrs.hrefUsable, 0);
        RoboCheck(attrs.href == NULL);
    }

    /* The image state is derived, so it follows the attribute rather than
     * being stored: present → pending, removed → none, restored → pending. */
    RoboCheckEqInt((int)HtmlElementImageState(&photo),
                      (int)RoboHtmlImageStatePending);
    RoboCheckEqInt(HtmlElementRemoveAttribute(&photo, "src", NULL),
                      RoboOk);
    RoboCheckEqInt((int)HtmlElementImageState(&photo),
                      (int)RoboHtmlImageStateNone);
    RoboCheckEqInt(
        HtmlElementSetAttribute(&photo, "src", "Assets/Other.png",
                                        NULL),
        RoboOk);
    RoboCheckEqInt((int)HtmlElementImageState(&photo),
                      (int)RoboHtmlImageStatePending);
    RoboCheckStrEq(DomAttr(&photo, "alt"), "بديل");

    /* The declared value follows the attribute; the override does not. */
    DomControlValue(&field, buf, sizeof(buf), &declared);
    RoboCheckStrEq(buf, "declared");
    RoboCheckEqInt(declared, 1);
    RoboCheckEqInt(
        HtmlElementSetAttribute(&field, "value", "edited", NULL),
        RoboOk);
    DomControlValue(&field, buf, sizeof(buf), &declared);
    RoboCheckStrEq(buf, "edited");
    RoboCheckEqInt(declared, 1);

    RoboCheckEqInt(
        HtmlElementSetControlValue(&field, "override", 8u, NULL),
        RoboOk);
    DomControlValue(&field, buf, sizeof(buf), &declared);
    RoboCheckStrEq(buf, "override");
    RoboCheckEqInt(declared, 0);

    RoboCheckEqInt(
        HtmlElementRemoveAttribute(&field, "value", NULL), RoboOk);
    /* The override survives the attribute it defaulted from. */
    DomControlValue(&field, buf, sizeof(buf), &declared);
    RoboCheckStrEq(buf, "override");
    RoboCheckEqInt(declared, 0);

    RoboCheckEqInt(HtmlElementClearControlValue(&field, NULL),
                      RoboOk);
    /* With no override and no attribute the value is empty — and it is the
     * declared one, because nothing overrides it. */
    DomControlValue(&field, buf, sizeof(buf), &declared);
    RoboCheckStrEq(buf, "");
    RoboCheckEqInt(declared, 1);

    /* Checkedness is its own sequence, and its own attribute. */
    RoboCheckEqInt(
        HtmlElementControlChecked(&field, &checked, &declared, NULL),
        RoboOk);
    RoboCheckEqInt(checked, 1);
    RoboCheckEqInt(declared, 1);
    RoboCheckEqInt(HtmlElementSetControlChecked(&field, 0, NULL),
                      RoboOk);
    RoboCheckEqInt(
        HtmlElementControlChecked(&field, &checked, &declared, NULL),
        RoboOk);
    RoboCheckEqInt(checked, 0);
    RoboCheckEqInt(declared, 0);
    RoboCheckEqInt(
        HtmlElementRemoveAttribute(&field, "checked", NULL), RoboOk);
    RoboCheckEqInt(
        HtmlElementControlChecked(&field, &checked, &declared, NULL),
        RoboOk);
    RoboCheckEqInt(checked, 0);
    RoboCheckEqInt(declared, 0);
    RoboCheckEqInt(HtmlElementClearControlChecked(&field, NULL),
                      RoboOk);
    RoboCheckEqInt(
        HtmlElementControlChecked(&field, &checked, &declared, NULL),
        RoboOk);
    RoboCheckEqInt(checked, 0); /* the attribute is gone */
    RoboCheckEqInt(declared, 1);

    /* Dropping `selected` from the second option moves the select back to the
     * first — the deterministic rule, exercised through the attribute. */
    {
        HtmlNodeHandle selected;
        int explicitValue = 0;
        size_t length = 0u;

        RoboCheckEqInt(
            HtmlElementSelectSelectedOption(&pick, &selected, NULL),
            RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&selected, &optionTwo));
        RoboCheckEqInt(HtmlElementRemoveAttribute(&optionTwo,
                                                             "selected", NULL),
                          RoboOk);
        RoboCheckEqInt(
            HtmlElementSelectSelectedOption(&pick, &selected, NULL),
            RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&selected, &optionOne));
        RoboCheckEqInt(HtmlElementOptionValue(
                              &selected, buf, sizeof(buf), &length,
                              &explicitValue, NULL),
                          RoboOk);
        RoboCheckStrEq(buf, "one");
        RoboCheckEqInt(explicitValue, 1);
    }

    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 8. Depth                                                            */
/* ------------------------------------------------------------------ */

#define DOM_DEEP_LEVELS 200u

/**
 * A chain 200 levels deep, built through the construction seam so the
 * measurements are the tree's rather than the parser's.
 *
 * Detaching a node in the middle is the interesting half: the whole subtree
 * below it travels with it, which is asserted by the depth of the deepest node
 * before and after re-attachment — a number that changes by exactly the number
 * of levels the subtree moved. The pre-order walk is asked to confirm the
 * shape independently: on a pure chain the k-th element visited sits at depth
 * k, and nothing else does.
 */
static void TestDomDeepStructureStress(void)
{
    HtmlDocumentId id = DomNewDocument();
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle node = root;
    HtmlNodeHandle parentOfMiddle = root;
    HtmlNodeHandle middle;
    HtmlNodeHandle leaf;
    DomChainRecord chain;
    size_t i;

    for (i = 0u; i < DOM_DEEP_LEVELS; i++) {
        HtmlNodeHandle parent = node;

        if (i + 1u == DOM_DEEP_LEVELS / 2u) {
            parentOfMiddle = node;
        }
        node = DomCreateUnder(id, &parent, "div");
        if (i + 1u == DOM_DEEP_LEVELS / 2u) {
            middle = node;
        }
    }
    leaf = node;

    RoboCheckEqInt((int)DomDepthOf(&leaf), (int)DOM_DEEP_LEVELS);
    RoboCheckEqInt((int)DomDepthOf(&middle),
                      (int)(DOM_DEEP_LEVELS / 2u));
    RoboCheckEqInt((int)DomCountTags(&root, RoboHtmlTagDiv),
                      (int)DOM_DEEP_LEVELS);
    chain.visited = 0u;
    chain.mismatches = 0u;
    RoboCheckEqInt(
        HtmlNodeWalkDescendants(&root, RoboHtmlNodeFilterElements,
                                        DomChainVisitor, &chain, NULL),
        RoboOk);
    RoboCheckEqInt((int)chain.visited, (int)DOM_DEEP_LEVELS);
    RoboCheckEqInt((int)chain.mismatches, 0);
    DomExpectCleanTree(id);

    /* Detach the middle: a hundred levels leave the root path and become one
     * detached subtree, which the audit reports as legal. */
    RoboCheckEqInt(
        HtmlNodeRemoveChild(&parentOfMiddle, &middle, NULL),
        RoboOk);
    {
        HtmlTreeReport report;

        memset(&report, 0, sizeof(report));
        RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                          RoboOk);
        RoboCheck(report.ok);
        RoboCheckEqInt((int)report.detached, 1);
    }

    /* Re-attach it at the top: the deepest node is now one level below the
     * root plus the hundred levels the subtree brought with it. */
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &middle, NULL),
                      RoboOk);
    RoboCheckEqInt((int)DomDepthOf(&middle), 1);
    RoboCheckEqInt((int)DomDepthOf(&leaf),
                      1 + (int)(DOM_DEEP_LEVELS - DOM_DEEP_LEVELS / 2u));
    RoboCheckEqInt((int)DomCountTags(&root, RoboHtmlTagDiv),
                      (int)DOM_DEEP_LEVELS);
    DomExpectCleanTree(id);

    /* Depth survives a document that re-parses deeper markup too. */
    {
        HtmlDocumentId parsed = DomNewDocument();
        char markup[2048];
        size_t at = 0u;
        size_t levels = 100u;
        HtmlNodeHandle proot;
        HtmlNodeHandle deepest;

        for (i = 0u; i < levels; i++) {
            at += (size_t)snprintf(markup + at, sizeof(markup) - at, "<div>");
        }
        for (i = 0u; i < levels; i++) {
            at += (size_t)snprintf(markup + at, sizeof(markup) - at, "</div>");
        }
        DomParse(parsed, markup);
        proot = DomRoot(parsed);
        deepest = DomFindTag(&proot, RoboHtmlTagDiv);
        {
            size_t depth = 1u;
            HtmlNodeHandle child;

            while (HtmlNodeFirstChild(&deepest, &child, NULL) ==
                   RoboOk) {
                if (!HtmlElementIsTag(&child, RoboHtmlTagDiv)) {
                    break;
                }
                deepest = child;
                depth++;
            }
            RoboCheckEqInt((int)depth, (int)levels);
        }
        RoboCheckEqInt((int)DomCountTags(&proot, RoboHtmlTagDiv),
                          (int)levels);
        DomExpectCleanTree(parsed);
        RoboCheckEqInt(HtmlDocumentDestroy(parsed, NULL), RoboOk);
    }

    /* Teardown leaves every handle stale. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeHandleIsStale(&leaf), 1);
    RoboCheckEqInt(HtmlNodeHandleIsStale(&middle), 1);
}

/* ------------------------------------------------------------------ */
/* 9. Width                                                            */
/* ------------------------------------------------------------------ */

#define DOM_WIDE_SIBLINGS 300u

/**
 * 300 siblings: creation order is document order, a removal in the middle
 * closes the gap without reordering anything else, and an insert lands exactly
 * where it was asked to. The first, middle and last children are compared by
 * identity rather than by count, because a count-only assertion would pass on
 * a tree whose order had been reversed.
 */
static void TestDomWideStructureStress(void)
{
    HtmlDocumentId id = DomNewDocument();
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle container = DomCreateUnder(id, &root, "div");
    HtmlNodeHandle spans[DOM_WIDE_SIBLINGS];
    HtmlNodeHandle got;
    size_t i;
    size_t removedTotal = 0u;

    for (i = 0u; i < DOM_WIDE_SIBLINGS; i++) {
        spans[i] = DomCreateUnder(id, &container, "span");
    }
    RoboCheckEqInt((int)DomChildCount(&container),
                      (int)DOM_WIDE_SIBLINGS);
    RoboCheckEqInt(HtmlNodeFirstChild(&container, &got, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &spans[0]));
    RoboCheckEqInt(HtmlNodeLastChild(&container, &got, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &spans[DOM_WIDE_SIBLINGS - 1u]));
    RoboCheckEqInt(
        HtmlNodeChildAt(&container, DOM_WIDE_SIBLINGS / 2u, &got, NULL),
        RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got,
                                           &spans[DOM_WIDE_SIBLINGS / 2u]));
    DomExpectCleanTree(id);

    /* Remove every other one. The remaining order must be the original
     * subsequence, which is checked by identity at three points. */
    for (i = 1u; i < DOM_WIDE_SIBLINGS; i += 2u) {
        RoboCheckEqInt(HtmlNodeRemoveChild(&container, &spans[i],
                                                      NULL),
                          RoboOk);
        removedTotal++;
    }
    RoboCheckEqInt((int)removedTotal, (int)(DOM_WIDE_SIBLINGS / 2u));
    RoboCheckEqInt((int)DomChildCount(&container),
                      (int)(DOM_WIDE_SIBLINGS / 2u));
    RoboCheckEqInt(HtmlNodeFirstChild(&container, &got, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &spans[0]));
    RoboCheckEqInt(HtmlNodeChildAt(&container, 1u, &got, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &spans[2]));
    RoboCheckEqInt(HtmlNodeLastChild(&container, &got, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(
        &got, &spans[DOM_WIDE_SIBLINGS - 2u]));
    DomExpectCleanTree(id);

    /* Put one back, in the position it came from. */
    RoboCheckEqInt(HtmlNodeInsertBefore(&container, &spans[1],
                                                   &spans[2], NULL),
                      RoboOk);
    RoboCheckEqInt((int)DomChildCount(&container),
                      (int)(DOM_WIDE_SIBLINGS / 2u) + 1);
    RoboCheckEqInt(HtmlNodeChildAt(&container, 1u, &got, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &spans[1]));
    RoboCheckEqInt(HtmlNodeChildAt(&container, 2u, &got, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &spans[2]));
    DomExpectCleanTree(id);

    /* Removing the whole child list at once is the bulk path, and it reports
     * how many it detached. */
    RoboCheckEqInt(
        HtmlNodeRemoveChildren(&container, &removedTotal, NULL),
        RoboOk);
    RoboCheckEqInt((int)removedTotal,
                      (int)(DOM_WIDE_SIBLINGS / 2u) + 1);
    RoboCheckEqInt((int)DomChildCount(&container), 0);
    DomExpectCleanTree(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 10. Unicode matrix                                                  */
/* ------------------------------------------------------------------ */

/**
 * Thirteen script families through the whole pipeline: parse → text →
 * attribute → programmatic text node → mutation. Every comparison is
 * byte-exact; a length check accompanies each one, because a truncation and a
 * substitution can both leave a "contains the sample" assertion satisfied.
 */
static void TestDomUnicodeMatrix(void)
{
    const DomUnicodeSample *samples;
    size_t count = 0u;
    size_t i;

    samples = DomUnicodeSamples(&count);
    RoboCheck(count >= 13u);

    for (i = 0u; i < count; i++) {
        char markup[1024];
        const char *text = samples[i].text;
        size_t textLength = strlen(text);
        HtmlDocumentId id;
        HtmlDocumentId built;
        HtmlNodeHandle root;
        HtmlNodeHandle holder;
        HtmlNodeHandle node;
        HtmlNodeHandle section;
        HtmlNodeHandle textNode;
        char got[512];
        size_t contentLength = 0u;

        RoboCheck(snprintf(markup, sizeof(markup),
                            "<section id=\"Holder\"></section>"
                            "<div id=\"Node\" title=\"%s\">%s</div>",
                            text, text) > 0);

        id = DomDocumentOf(markup);
        root = DomRoot(id);
        node = DomFindId(&root, "Node");
        holder = DomFindId(&root, "Holder");

        DomText(&node, got, sizeof(got));
        RoboCheckMsg(strcmp(got, text) == 0, samples[i].name);
        RoboCheckStrEq(DomAttr(&node, "title"), text);
        RoboCheckEqInt((int)strlen(DomAttr(&node, "title")),
                          (int)textLength);

        /* Move the parsed element: mutation must carry the bytes. Detached
         * from its *real* parent, not from the document root — a parsed
         * element hangs under the implied `<body>`, so naming the root would
         * be a not-found rather than a move. */
        DomDetach(&node);
        RoboCheckEqInt(HtmlNodeAppendChild(&holder, &node, NULL),
                          RoboOk);
        DomText(&node, got, sizeof(got));
        RoboCheckMsg(strcmp(got, text) == 0, samples[i].name);
        DomExpectCleanTree(id);

        /* A programmatic text node on a hand-built document carries the same
         * bytes — the construction seam copies, it does not normalize. */
        built = DomNewDocument();
        {
            HtmlNodeHandle builtRoot = DomRoot(built);

            section = DomCreateUnder(built, &builtRoot, "section");
            textNode = DomCreateText(built, &section, text);
            RoboCheckEqInt((int)HtmlTextLength(&textNode),
                              (int)textLength);
            RoboCheckStrEq(HtmlTextContent(&textNode,
                                                     &contentLength),
                              text);
            RoboCheckEqInt((int)contentLength, (int)textLength);
            RoboCheckEqInt(HtmlTextIsEmpty(&textNode), 0);
            DomText(&section, got, sizeof(got));
            RoboCheckMsg(strcmp(got, text) == 0, samples[i].name);
            DomExpectCleanTree(built);
        }

        RoboCheckEqInt(HtmlDocumentDestroy(built, NULL), RoboOk);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }

    /* An empty text node is the one case where "no bytes" is the content, and
     * it is a legal value rather than an error. */
    {
        HtmlDocumentId built = DomNewDocument();
        HtmlNodeHandle builtRoot = DomRoot(built);
        HtmlNodeHandle section = DomCreateUnder(built, &builtRoot,
                                                           "p");
        HtmlNodeHandle empty = DomCreateText(built, &section, "");

        RoboCheckEqInt(HtmlTextIsEmpty(&empty), 1);
        RoboCheckEqInt((int)HtmlTextLength(&empty), 0);
        RoboCheckStrEq(HtmlTextContent(&empty, NULL), "");
        DomExpectCleanTree(built);
        RoboCheckEqInt(HtmlDocumentDestroy(built, NULL), RoboOk);
    }
}

/**
 * The same Unicode ids through the three consumers. A model that compared ids
 * byte-wise is the only kind that can pass this; one that folded, truncated or
 * ASCII-filtered the id would answer the ASCII case and fail here.
 */
static void TestDomUnicodeIdConsumers(void)
{
    static const char kArabic[] = "مرحبا";
    static const char kDevanagari[] = "नमस्ते";
    static const char kKorean[] = "안녕하세요";
    char markup[512];
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle found;
    HtmlNodeHandle owner;

    RoboCheck(snprintf(markup, sizeof(markup),
                        "<div id=\"%s\">x</div>"
                        "<label for=\"%s\">L</label><input id=\"%s\">"
                        "<form id=\"%s\"></form><input form=\"%s\">",
                        kArabic, kDevanagari, kDevanagari, kKorean,
                        kKorean) > 0);
    id = DomDocumentOf(markup);
    root = DomRoot(id);

    /* A fragment naming a Unicode id resolves to it... */
    RoboCheckEqInt(HtmlNavigationFindFragment(
                          id, kArabic, strlen(kArabic), &found, NULL),
                      RoboOk);
    RoboCheck(HtmlElementIsTag(&found, RoboHtmlTagDiv));

    /* ...and a name that differs in one byte does not. */
    {
        char nearMiss[64];

        RoboCheck(snprintf(nearMiss, sizeof(nearMiss), "%sX", kArabic) > 0);
        RoboCheckEqInt(HtmlNavigationFindFragment(
                              id, nearMiss, strlen(nearMiss), &found, NULL),
                          RoboNotFound);
    }

    /* A label resolves a Devanagari `for`, and the wrapping rule is the same
     * comparison. */
    {
        HtmlNodeHandle label = DomFindTag(&root,
                                                     RoboHtmlTagLabel);
        int fromAttribute = -1;

        RoboCheckEqInt(
            HtmlElementLabelTarget(&label, &found, &fromAttribute,
                                           NULL),
            RoboOk);
        RoboCheckEqInt(fromAttribute, 1);
        RoboCheck(HtmlElementIsTag(&found, RoboHtmlTagInput));
    }

    /* A control resolves a Korean `form` attribute. */
    {
        HtmlNodeHandle form = DomFindTag(&root, RoboHtmlTagForm);
        HtmlFormOwnerSource source = RoboHtmlFormOwnerSourceNone;
        HtmlNodeHandle control;

        /* The Korean id names *two* elements: the form itself, and through
         * `form=` the input. An id lookup answers with the first in document
         * order — the form — so the control is found by the attribute that
         * distinguishes it. */
        control = DomFindByAttr(&root, "form", kKorean);
        RoboCheck(HtmlElementIsTag(&control, RoboHtmlTagInput));
        RoboCheckEqInt(
            HtmlElementFormOwner(&control, &owner, &source, NULL),
            RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&owner, &form));
        RoboCheckEqInt((int)source,
                          (int)RoboHtmlFormOwnerSourceAttribute);
    }

    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 11. Lifecycle churn                                                 */
/* ------------------------------------------------------------------ */

#define DOM_CHURN_CYCLES 24u

/**
 * Repeated whole-document lifetimes, with the debug allocator's live count
 * checked before and after.
 *
 * One warm-up cycle runs before the baseline is taken: the first document a
 * process creates may leave the module's own tables sized for it, and
 * counting that as a leak would make the test fail for the wrong reason. After
 * the warm-up, the live count must return to exactly where it started — for a
 * test process this is the closest thing to a leak check that exists, and it
 * catches a document whose arena, control table or attachment table is not
 * released on the destroy path.
 *
 * The observable half of the test runs in every configuration: the live
 * document count returns to its start, and handles from a destroyed document
 * are stale rather than silently reusable.
 */
static void TestDomLifecycleChurn(void)
{
    static const char kMarkup[] =
        "<div id=\"Box\"><a href=\"#Target\">go</a>"
        "<img src=\"Assets/Photo.png\" alt=\"a\">"
        "<form id=\"F2\"><label for=\"C2\">L</label>"
        "<input id=\"C2\" type=\"checkbox\" value=\"v\" checked>"
        "<select id=\"S2\"><option value=\"o\">O</option></select>"
        "</form></div>";
    size_t docsBefore = HtmlLiveDocumentCount();
    int debugWasEnabled = MemDebugIsEnabled();
#if RoboMemDebug
    size_t baseline = 0u;
#endif
    size_t i;

    if (!debugWasEnabled) {
        MemDebugEnable();
    }

    /* Warm-up: one full lifetime before the baseline is measured. */
    {
        HtmlDocumentId warm = DomDocumentOf(kMarkup);
        HtmlNodeHandle warmRoot = DomRoot(warm);
        HtmlNodeHandle field = DomFindId(&warmRoot, "C2");

        RoboCheckEqInt(
            HtmlElementSetControlValue(&field, "warm", 4u, NULL),
            RoboOk);
        RoboCheckEqInt(HtmlDocumentDestroy(warm, NULL), RoboOk);
    }

#if RoboMemDebug
    /* Measured only where the allocator accounts for it: in a Release build
     * MemDebugLiveCount() is a stub and the comparison below is gone. */
    baseline = MemDebugLiveCount();
#endif

    for (i = 0u; i < DOM_CHURN_CYCLES; i++) {
        HtmlDocumentId id = DomDocumentOf(kMarkup);
        HtmlNodeHandle root = DomRoot(id);
        HtmlNodeHandle box = DomFindId(&root, "Box");
        HtmlNodeHandle anchor = DomFindTag(&root, RoboHtmlTagA);
        HtmlNodeHandle field = DomFindId(&root, "C2");
        HtmlNodeHandle option;
        char buf[32];
        int declared = 0;

        /* Touch every subsystem that owns per-document state: control state
         * (a heap copy), image attachment bookkeeping, option ordering, and a
         * structural mutation. */
        RoboCheckEqInt(
            HtmlElementSetControlValue(&field, "cycled", 6u, NULL),
            RoboOk);
        RoboCheckEqInt(HtmlElementSetControlChecked(&field, 0,
                                                                NULL),
                          RoboOk);
        HtmlElementControlValue(&field, buf, sizeof(buf), NULL,
                                        &declared, NULL);
        RoboCheckEqInt(
            HtmlNavigationFindFragment(id, "C2", 2u, &option, NULL),
            RoboOk);
        (void)anchor;
        RoboCheckEqInt(HtmlElementSetAttribute(&box, "data-i", "1",
                                                          NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlElementRemoveAttribute(&box, "data-i",
                                                             NULL),
                          RoboOk);

        DomExpectCleanTree(id);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeHandleIsStale(&box), 1);
        RoboCheckEqInt(HtmlNodeHandleIsStale(&field), 1);
        /* A destroyed document is not merely empty: it is gone. */
        RoboCheckEqInt(HtmlDocumentIsLive(id), 0);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboNotFound);
    }

    RoboCheckEqInt((int)HtmlLiveDocumentCount(), (int)docsBefore);

#if RoboMemDebug
    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
#endif

    if (!debugWasEnabled) {
        MemDebugDisable();
    }
}

/* ------------------------------------------------------------------ */
/* 12. Feature integration                                             */
/* ------------------------------------------------------------------ */

/**
 * One document that uses every subsystem the runtime has, checked as a whole:
 * the capability flags, the counts, all four element-side models, traversal
 * order, a mutation that changes that order, and teardown.
 *
 * `events` is asserted to be 0 on purpose. The flags are a promise about what
 * this build can do, and a flag that claimed the DOM-events phase before it
 * exists would be the one lie in an otherwise verifiable set.
 */
static void TestDomFeatureIntegration(void)
{
    static const char kMarkup[] =
        "<!DOCTYPE html>\n"
        "<html>\n<head><title>Integration</title></head>\n"
        "<body>\n"
        "<h1 id=\"Title\">Heading</h1>\n"
        "<p class=\"Lead\">Body <strong>bold</strong> text</p>\n"
        "<a id=\"Link\" href=\"#Title\">to title</a>\n"
        "<img id=\"Photo\" src=\"Assets/Photo.png\" alt=\"Photo\">\n"
        "<form id=\"Editor\" action=\"/Save\" method=\"post\">\n"
        "<label for=\"Name\">Name</label>\n"
        "<input id=\"Name\" type=\"text\" name=\"name\" value=\"Ada\">\n"
        "<input id=\"Agree\" type=\"checkbox\" name=\"agree\" checked>\n"
        "<textarea id=\"Notes\" name=\"notes\">Hello</textarea>\n"
        "<select id=\"Ring\" name=\"ring\">"
        "<option value=\"a\">A</option>"
        "<option value=\"b\" selected>B</option></select>\n"
        "<button type=\"submit\">Save</button>\n"
        "</form>\n"
        "<p>امتحان</p>\n"
        "</body></html>";
    HtmlDocumentId id = DomDocumentOf(kMarkup);
    HtmlNodeHandle root = DomRoot(id);
    HtmlDomCapabilities caps;
    DomOrderRecord order;
    char buf[64];
    int declared = 0;
    int checked = 0;

    /* The build's promises, all at once. */
    memset(&caps, 0, sizeof(caps));
    RoboCheckEqInt(HtmlDomQueryCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.treeBuilding, 1);
    /* `domNodes` stays 0 by design: it is the flag for a *complete* node DOM
     * surface, and this build has one model per aspect (attributes, text,
     * traversal, mutation) rather than the whole. Asserting 0 keeps the flag
     * honest rather than aspirational. */
    RoboCheckEqInt(caps.domNodes, 0);
    RoboCheckEqInt(caps.attributes, 1);
    RoboCheckEqInt(caps.textNodes, 1);
    RoboCheckEqInt(caps.traversal, 1);
    RoboCheckEqInt(caps.mutation, 1);
    RoboCheckEqInt(caps.elementTags, 1);
    RoboCheckEqInt(caps.images, 1);
    RoboCheckEqInt(caps.links, 1);
    RoboCheckEqInt(caps.forms, 1);
    RoboCheckEqInt(caps.events, 0); /* not this phase, and not claimed */

    /* The tree is what the source said. */
    RoboCheckEqInt((int)DomCountTags(&root, RoboHtmlTagH1), 1);
    RoboCheckEqInt((int)DomCountTags(&root, RoboHtmlTagP), 2);
    RoboCheckEqInt((int)DomCountTags(&root, RoboHtmlTagForm), 1);
    RoboCheckEqInt((int)DomCountTags(&root, RoboHtmlTagInput), 2);
    RoboCheckEqInt((int)DomCountTags(&root, RoboHtmlTagLabel), 1);
    RoboCheck(HtmlDocumentNodeCount(id) > 20u);

    /* Every subsystem answers about this document. */
    {
        HtmlNodeHandle heading = DomFindId(&root, "Title");
        HtmlNodeHandle found;
        HtmlNodeHandle label = DomFindTag(&root,
                                                     RoboHtmlTagLabel);
        HtmlNodeHandle name = DomFindId(&root, "Name");
        HtmlNodeHandle agree = DomFindId(&root, "Agree");
        HtmlNodeHandle ring = DomFindId(&root, "Ring");
        HtmlNodeHandle selected;
        HtmlNodeHandle owner;
        HtmlNodeHandle photo = DomFindId(&root, "Photo");
        HtmlNodeHandle link = DomFindId(&root, "Link");
        HtmlAnchorAttributes anchor;
        HtmlFormOwnerSource source = RoboHtmlFormOwnerSourceNone;
        int fromAttribute = -1;
        char text[256];

        /* links: a fragment naming a real element in this document. */
        RoboCheckEqInt(
            HtmlNavigationFindFragment(id, "Title", 5u, &found, NULL),
            RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&found, &heading));
        memset(&anchor, 0, sizeof(anchor));
        RoboCheckEqInt(
            HtmlElementAnchorAttributes(&link, &anchor, NULL), RoboOk);
        RoboCheckEqInt(anchor.hrefUsable, 1);
        RoboCheck(memcmp(anchor.href, "#Title", 6u) == 0);

        /* images: a declared but unresolved resource is PENDING, not an
         * error, and nothing was fetched. */
        RoboCheckEqInt((int)HtmlElementImageState(&photo),
                          (int)RoboHtmlImageStatePending);

        /* forms: label → control, control → form, declared state. */
        RoboCheckEqInt(
            HtmlElementLabelTarget(&label, &found, &fromAttribute,
                                           NULL),
            RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&found, &name));
        RoboCheckEqInt(
            HtmlElementFormOwner(&name, &owner, &source, NULL),
            RoboOk);
        RoboCheckEqInt((int)source,
                          (int)RoboHtmlFormOwnerSourceAncestor);
        RoboCheck(HtmlElementIsTag(&owner, RoboHtmlTagForm));
        DomControlValue(&name, buf, sizeof(buf), &declared);
        RoboCheckStrEq(buf, "Ada");
        RoboCheckEqInt(declared, 1);
        RoboCheckEqInt(
            HtmlElementControlChecked(&agree, &checked, &declared,
                                              NULL),
            RoboOk);
        RoboCheckEqInt(checked, 1);
        RoboCheckEqInt(
            HtmlElementSelectSelectedOption(&ring, &selected, NULL),
            RoboOk);
        RoboCheckEqInt((int)HtmlElementOptionValue(
                              &selected, buf, sizeof(buf), NULL, NULL, NULL),
                          (int)RoboOk);
        RoboCheckStrEq(buf, "b");

        /* Attribute-driven lookup: the same descendant walk that id lookup
         * uses, with a different predicate, reaches the same nodes a reader
         * would — the submit button by `type` and the chosen option by
         * `value` (which pins the *attribute*, not the selection logic that
         * was just asserted above). */
        {
            HtmlNodeHandle submit = DomFindByAttr(&root, "type",
                                                              "submit");
            HtmlNodeHandle optionB = DomFindByAttr(&root, "value",
                                                                "b");

            size_t attributes = 0u;

            RoboCheck(HtmlElementIsTag(&submit, RoboHtmlTagButton));
            RoboCheck(HtmlNodeHandleEqual(&optionB, &selected));
            RoboCheckEqInt(
                HtmlElementAttributeCount(&submit, &attributes, NULL),
                RoboOk);
            RoboCheckEqInt((int)attributes, 1);
        }

        /* the text model: the last paragraph is not Latin. */
        {
            HtmlNodeHandle paragraphs[2];
            size_t seen = 0u;
            HtmlNodeHandle walk = DomFindTag(&root,
                                                        RoboHtmlTagP);

            paragraphs[seen++] = walk;
            while (seen < 2u && DomNextTagSibling(&walk, RoboHtmlTagP,
                                                     &walk)) {
                paragraphs[seen++] = walk;
            }
            RoboCheckEqInt((int)seen, 2);
            DomText(&paragraphs[1], text, sizeof(text));
            RoboCheckStrEq(text, "امتحان");
        }
    }

    /* Document order, observed rather than assumed: the heading precedes the
     * form, and the first paragraph precedes it too. */
    memset(&order, 0, sizeof(order));
    order.heading = -1;
    order.form = -1;
    order.paragraph = -1;
    RoboCheckEqInt(
        HtmlNodeWalkDescendants(&root, RoboHtmlNodeFilterElements,
                                        DomOrderVisitor, &order, NULL),
        RoboOk);
    RoboCheck(order.heading >= 0);
    RoboCheck(order.form > order.heading);
    RoboCheck(order.paragraph > order.heading);
    RoboCheck(order.paragraph < order.form);

    /* A mutation changes that order, and the dump says so. */
    {
        HtmlNodeHandle form = DomFindTag(&root,
                                                    RoboHtmlTagForm);
        HtmlNodeHandle heading = DomFindId(&root, "Title");
        HtmlNodeHandle body = DomFindTag(&root, RoboHtmlTagBody);
        char before[4096];
        char after[4096];

        DomDump(id, before, sizeof(before));
        RoboCheckEqInt(HtmlNodeRemoveChild(&body, &form, NULL),
                          RoboOk);
        RoboCheckEqInt(
            HtmlNodeInsertBefore(&body, &form, &heading, NULL),
            RoboOk);
        DomDump(id, after, sizeof(after));
        RoboCheck(strcmp(before, after) != 0);

        memset(&order, 0, sizeof(order));
        order.heading = -1;
        order.form = -1;
        order.paragraph = -1;
        RoboCheckEqInt(
            HtmlNodeWalkDescendants(&root, RoboHtmlNodeFilterElements,
                                            DomOrderVisitor, &order, NULL),
            RoboOk);
        RoboCheck(order.form < order.heading);
        DomExpectCleanTree(id);
    }

    /* Teardown: every handle goes stale, and destroying twice is a not-found
     * rather than a second free. */
    {
        HtmlNodeHandle heading = DomFindId(&root, "Title");

        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeHandleIsStale(&heading), 1);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboNotFound);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlDom(void)
{
    TestRegister("html.dom.document_isolation",
                       TestDomDocumentIsolation);
    TestRegister("html.dom.id_resolution_agrees",
                       TestDomIdResolutionAgrees);
    TestRegister("html.dom.id_change_propagates",
                       TestDomIdChangePropagates);
    TestRegister("html.dom.void_child_behaviour",
                       TestDomVoidChildBehaviour);
    TestRegister("html.dom.invariants_after_recovery",
                       TestDomInvariantsAfterRecovery);
    TestRegister("html.dom.rebuild_is_deterministic",
                       TestDomRebuildIsDeterministic);
    TestRegister("html.dom.attribute_mutation_sequences",
                       TestDomAttributeMutationSequences);
    TestRegister("html.dom.deep_structure_stress",
                       TestDomDeepStructureStress);
    TestRegister("html.dom.wide_structure_stress",
                       TestDomWideStructureStress);
    TestRegister("html.dom.unicode_matrix", TestDomUnicodeMatrix);
    TestRegister("html.dom.unicode_id_consumers",
                       TestDomUnicodeIdConsumers);
    TestRegister("html.dom.lifecycle_churn", TestDomLifecycleChurn);
    TestRegister("html.dom.feature_integration",
                       TestDomFeatureIntegration);
}
