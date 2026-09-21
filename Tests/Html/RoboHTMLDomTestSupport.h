/*
 * RoboHTML DOM test support (Phase 8.15).
 *
 * Shared fixtures for the DOM-level suites — the ones that test the model as
 * a *system* (documents, isolation, id resolution, invariants, determinism)
 * rather than one phase's surface in isolation.
 *
 * It is deliberately not a framework: it adds no runner, no registration, no
 * reporter and no assertion vocabulary. Every helper below does one of three
 * things the per-phase suites each re-implemented — begin the fixture, parse
 * a document, and look something up — and everything is `static inline`, so a
 * suite that includes this header pays only for the helpers it calls and no
 * object is emitted for the ones it does not. `-Wall -Wextra -Werror` is on
 * for the host build, which is why `static inline` (rather than plain
 * `static`) matters here.
 *
 * The helpers parse real HTML through the real tokenizer and tree builder.
 * A helper that hand-built nodes would let a suite pass while the parser
 * integration it claims to test was broken.
 */

#ifndef ROBO_HTML_DOM_TEST_SUPPORT_H
#define ROBO_HTML_DOM_TEST_SUPPORT_H

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

/** Bring the foundation up. Idempotent; every suite calls it first. */
static inline void DomFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

/** A new empty document, asserted created. */
static inline HtmlDocumentId DomNewDocument(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;

    DomFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    return id;
}

/**
 * Parse @p text into @p id through the real source → tokenizer → tree
 * builder path, asserting success.
 *
 * Success is asserted rather than tolerated: every input these suites feed it
 * is expected to parse. Malformed *markup* is not a failing build — the tree
 * builder recovers — so a case that means "this recovers" passes through here
 * too, and the recovery itself is asserted by the caller.
 */
static inline void DomParse(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

/** Create a document and parse @p text into it in one step. */
static inline HtmlDocumentId DomDocumentOf(const char *text)
{
    HtmlDocumentId id = DomNewDocument();

    DomParse(id, text);
    return id;
}

/** The document root, asserted well-formed. */
static inline HtmlNodeHandle DomRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

/* ------------------------------------------------------------------ */
/* Lookup                                                              */
/* ------------------------------------------------------------------ */

/** One predicate argument: a tag to match. */
typedef struct domTagQuery {
    HtmlTag tag;
} DomTagQuery;

static inline int DomPredicateTag(const HtmlNodeView *view,
                                    void *user)
{
    const DomTagQuery *q = (const DomTagQuery *)user;

    return HtmlElementIsTag(&view->handle, q->tag);
}

/**
 * First element with tag @p tag at or below @p start.
 * @return 1 and a filled @p out when found; 0 (and a cleared @p out) when not.
 */
static inline int DomTryFindTag(const HtmlNodeHandle *start,
                                   HtmlTag tag,
                                   HtmlNodeHandle *out)
{
    DomTagQuery q;
    Result r;

    q.tag = tag;
    HtmlNodeHandleClear(out);
    r = HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                                  DomPredicateTag, &q, out, NULL);
    if (r == RoboNotFound) {
        return 0;
    }
    RoboCheckEqInt(r, RoboOk);
    return HtmlNodeHandleIsWellformed(out) ? 1 : 0;
}

/** As DomTryFindTag, but asserts the element exists. */
static inline HtmlNodeHandle DomFindTag(
    const HtmlNodeHandle *start, HtmlTag tag)
{
    HtmlNodeHandle out;

    RoboCheck(DomTryFindTag(start, tag, &out));
    return out;
}

/** One predicate argument: an `id` attribute value to match exactly. */
typedef struct domIdQuery {
    const char *id;
    size_t length;
} DomIdQuery;

/** Exact, case-sensitive comparison against the element's `id` attribute. */
static inline int DomPredicateId(const HtmlNodeView *view,
                                   void *user)
{
    const DomIdQuery *q = (const DomIdQuery *)user;
    size_t length = 0u;
    const char *value = HtmlElementAttributeValue(&view->handle, "id",
                                                          &length);

    if (!value || length != q->length) {
        return 0;
    }
    return memcmp(value, q->id, length) == 0;
}

/**
 * First element at or below @p start whose `id` attribute is exactly @p id,
 * in document order — the same rule the runtime's id consumers apply.
 * @return 1 and a filled @p out; 0 when nothing matches.
 */
static inline int DomTryFindId(const HtmlNodeHandle *start,
                                  const char *id,
                                  HtmlNodeHandle *out)
{
    DomIdQuery q;
    Result r;

    q.id = id;
    q.length = strlen(id);
    HtmlNodeHandleClear(out);
    r = HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                                  DomPredicateId, &q, out, NULL);
    if (r == RoboNotFound) {
        return 0;
    }
    RoboCheckEqInt(r, RoboOk);
    return HtmlNodeHandleIsWellformed(out) ? 1 : 0;
}

/** First element with the given `id` attribute, asserted to exist. */
static inline HtmlNodeHandle DomFindId(
    const HtmlNodeHandle *start, const char *id)
{
    HtmlNodeHandle out;

    RoboCheck(DomTryFindId(start, id, &out));
    return out;
}

/* ------------------------------------------------------------------ */
/* Counts and text                                                     */
/* ------------------------------------------------------------------ */

typedef struct domCountQuery {
    HtmlTag tag;
    size_t count;
} DomCountQuery;

static inline Result DomCountVisitor(
    const HtmlNodeView *view, size_t depth, void *user)
{
    DomCountQuery *q = (DomCountQuery *)user;

    (void)depth;
    if (HtmlElementIsTag(&view->handle, q->tag)) {
        q->count++;
    }
    return RoboOk;
}

/**
 * Number of elements of @p tag at or below @p start.
 *
 * Counted with the leaf walker rather than a recursive walk of the caller's
 * own, so the count is produced by the traversal the runtime ships.
 */
static inline size_t DomCountTags(const HtmlNodeHandle *start,
                                    HtmlTag tag)
{
    DomCountQuery q;

    q.tag = tag;
    q.count = 0u;
    RoboCheckEqInt(
        HtmlNodeWalkDescendants(start, RoboHtmlNodeFilterElements,
                                        DomCountVisitor, &q, NULL),
        RoboOk);
    return q.count;
}

/**
 * Subtree text of @p node into @p out, asserted to fit.
 *
 * @p outCap is treated as a hard ceiling: a truncated read would make a
 * Unicode-preservation assertion pass on a prefix, so this fails instead.
 */
static inline void DomText(const HtmlNodeHandle *node,
                            char *out,
                            size_t outCap)
{
    size_t length = 0u;

    RoboCheck(outCap > 0u);
    RoboCheckEqInt(
        HtmlNodeSubtreeText(node, out, outCap, &length, NULL),
        RoboOk);
    RoboCheckMsg(length < outCap, "subtree text was truncated");
    out[length] = '\0';
}

/** Borrowed attribute value of @p element, or NULL when absent. */
static inline const char *DomAttr(const HtmlNodeHandle *element,
                                   const char *name)
{
    return HtmlElementAttributeValue(element, name, NULL);
}

/* ------------------------------------------------------------------ */
/* Tree invariants                                                     */
/* ------------------------------------------------------------------ */

/**
 * Run the whole-tree invariant audit and assert it is clean.
 *
 * The audit is the runtime's own (`HtmlDocumentValidateTree`), so
 * this asks the model whether it agrees with itself rather than re-walking
 * the tree here and calling the answer independent. Detached subtrees are
 * legal and do not make the audit unclean; orphans, bad parents, bad counts,
 * cycles and bad links do, and any of them failing this call is a real
 * corruption rather than a test artefact.
 */
static inline void DomExpectCleanTree(HtmlDocumentId id)
{
    HtmlTreeReport report;

    memset(&report, 0, sizeof(report));
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                      RoboOk);
    RoboCheckMsg(report.ok, "tree invariant audit reported corruption");
    RoboCheckEqInt(report.orphans, 0u);
    RoboCheckEqInt(report.badLinks, 0u);
    RoboCheckEqInt(report.cycles, 0u);
    RoboCheckEqInt(report.badParents, 0u);
    RoboCheckEqInt(report.badCounts, 0u);
    RoboCheckEqInt(report.siblingMismatches, 0u);
    RoboCheck(report.rootOk);
}

/** Dump the document to @p out, asserted to fit. */
static inline void DomDump(HtmlDocumentId id,
                            char *out,
                            size_t outCap)
{
    size_t length = 0u;

    RoboCheckEqInt(
        HtmlTreeDumpToBuffer(id, out, outCap, &length, NULL),
        RoboOk);
    RoboCheckMsg(length < outCap, "tree dump was truncated");
    out[length] = '\0';
}

/* ------------------------------------------------------------------ */
/* Unicode samples                                                     */
/* ------------------------------------------------------------------ */

/**
 * One sample per script family the DOM has to carry intact.
 *
 * The bytes are UTF-8 literals rather than escapes, because the point of the
 * matrix is that these exact bytes survive; the name is what a failure
 * message should name.
 */
typedef struct domUnicodeSample {
    const char *name;
    const char *text;
} DomUnicodeSample;

static inline const DomUnicodeSample *DomUnicodeSamples(size_t *outCount)
{
    static const DomUnicodeSample samples[] = {
        {"english", "The quick brown fox"},
        {"french", "Élève à l'école — voilà"},
        {"german", "Grüße aus München"},
        {"arabic", "مرحبا بالعالم"},
        {"persian", "سلام دنیا"},
        {"urdu", "ہیلو دنیا"},
        {"hebrew", "שלום עולם"},
        {"devanagari", "नमस्ते दुनिया"},
        {"bengali", "হ্যালো বিশ্ব"},
        {"chinese", "你好，世界"},
        {"japanese", "こんにちは世界"},
        {"korean", "안녕하세요 세계"},
        {"emoji", "👩🏽‍🚀 family 👨‍👩‍👧‍👦"}
    };

    if (outCount) {
        *outCount = sizeof(samples) / sizeof(samples[0]);
    }
    return samples;
}

/* ------------------------------------------------------------------ */
/* The document of record                                              */
/* ------------------------------------------------------------------ */

/**
 * The checkpoint document: headings, paragraphs, links, a full form and a button.
 *
 * This is the document several suites' expectations are written against — the traversal case that walks
 * every element kind, and the cascade suite's selector repertoire, whose selectors name exactly these
 * elements (`h1 + p`, `form > label`, `select > option:last-child`, `a[href$="Settings"]`) and whose
 * element counts are the counts of *this* markup. It used to be served by the deleted demo layer
 * (`RoboDemo/`), and it lives here now because what a document fixture is is a document: the demo's
 * renderer went away with the demo, and the markup it happened to carry is a fact about these suites.
 *
 * Kept byte-for-byte identical to the document the demo carried, so no expectation moved with it.
 */
static inline const char *DomCheckpointHtml(void)
{
    static const char checkpoint[] =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "<title>RoboticelaOS Runtime</title>\n"
        "</head>\n"
        "<body>\n"
        "<h1>RoboticelaOS Runtime</h1>\n"
        "<p>Independent HTML rendering is working.</p>\n"
        "<p>RoboGraphics + RoboWindow + RoboHTML</p>\n"
        "<a href=\"/Settings\">Settings</a>\n"
        "<a href=\"#Overview\">Overview</a>\n"
        "<p id=\"Overview\">RoboHTML links resolve to real elements.</p>\n"
        "<form action=\"/Register\" method=\"post\" "
        "enctype=\"application/x-www-form-urlencoded\">\n"
        "<label for=\"DisplayName\">Display name</label>\n"
        "<input id=\"DisplayName\" type=\"text\" name=\"displayName\" "
        "placeholder=\"Your name\" required>\n"
        "<label for=\"Passphrase\">Passphrase</label>\n"
        "<input id=\"Passphrase\" type=\"password\" name=\"passphrase\">\n"
        "<label for=\"Revision\">Build revision</label>\n"
        "<input id=\"Revision\" type=\"number\" name=\"revision\" min=\"1\" "
        "max=\"999\" value=\"8\">\n"
        "<label for=\"Subscribed\">Platform notes</label>\n"
        "<input id=\"Subscribed\" type=\"checkbox\" name=\"subscribed\" "
        "checked>\n"
        "<label for=\"ReleaseRing\">Release ring</label>\n"
        "<input id=\"ReleaseRing\" type=\"radio\" name=\"ring\" "
        "value=\"Stable\" checked>\n"
        "<label for=\"Notes\">Notes</label>\n"
        "<textarea id=\"Notes\" name=\"notes\" rows=\"3\" cols=\"40\" "
        "placeholder=\"Anything else\"></textarea>\n"
        "<label for=\"Country\">Country</label>\n"
        "<select id=\"Country\" name=\"country\">\n"
        "<option value=\"pk\">Pakistan</option>\n"
        "<option value=\"us\" selected>United States</option>\n"
        "</select>\n"
        "<button type=\"submit\">Register</button>\n"
        "</form>\n"
        "<button>Demo Button</button>\n"
        "</body>\n"
        "</html>\n";

    return checkpoint;
}

#endif /* ROBO_HTML_DOM_TEST_SUPPORT_H */
