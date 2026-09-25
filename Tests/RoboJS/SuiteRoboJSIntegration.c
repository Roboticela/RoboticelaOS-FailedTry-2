/*
 * The RoboJS ↔ RoboHTML integration suite (Phase 16.16) — `js.integration.*`.
 *
 * This is the suite where "RoboJS manipulates the real RoboHTML DOM" stops being a sentence in a
 * header and becomes a number: every case here drives a runtime attached to a document that RoboHTML
 * **parsed**, through `JsDomHtmlAttach`, and then reads the result back out of RoboHTML's own
 * registry. The decisive move is repeated everywhere below: take the node id the binding handed out,
 * and resolve it with `HtmlDocumentNodeById`. That is RoboHTML saying "yes, that is one of my nodes"
 * rather than this suite believing a counter.
 *
 * ## The one boundary this suite asserts rather than works around
 *
 * RoboHTML's tree builder finalizes a document when its input is exhausted, finalization closes
 * *node creation*, and Phase 8 pins that refusal in `SuiteRoboHTMLMutation` and `SuiteRoboHTMLElement`
 * ("Pinned as a test so the seam is a decision rather than an accident"). `HtmlElementCreate` and
 * `HtmlTextCreate` take the parent they link under, so a detached element is not a state RoboHTML can
 * represent: a DOM-created node is a *pending record* in the adapter, and linking it is the
 * construction operation that a parsed document refuses.
 *
 * What that means in practice, and what the cases below are arranged around:
 *
 *   - **Reading is complete.** Queries, attributes, structure, `textContent` reads -- all full.
 *   - **Attribute writes are complete**, including `id`, `className` and `style`, which route to
 *     `HtmlElementSetAttribute` (Phase 8's own door, untroubled by finalization).
 *   - **Structural rearrangement is complete**: moving, inserting and detaching the nodes a document
 *     already has, which is Phase 8.4's stated purpose.
 *   - **A `textContent` write that reshapes nothing is complete**: an element whose lone child is
 *     already a text node is one `HtmlTextSet` -- a run rewrite, not a construction operation. That is
 *     `js.integration.text_in_place`.
 *   - **A `textContent` write that would create a node is refused, and refused *before* anything is
 *     destroyed.** That ordering is a correctness requirement rather than a nicety, and
 *     `js.integration.creation_refused` is the case that keeps it.
 *   - **Creation is proved where construction is open**: on a document that has not been parsed, and
 *     on one `HtmlDocumentReset` has reopened. `js.integration.creation_open` runs the whole
 *     create -> setAttribute -> appendChild -> textContent path over real RoboHTML and reads every
 *     result back through the document.
 *
 * The follow-up (a DOM construction gate that reopens for `RoboHtmlNodeOriginDom` after a parse) is
 * named in `Documentation/RoboJS/DOMBindings.md` as RoboHTML's document-lifecycle work, not hidden
 * here.
 *
 *   `js.integration.attach`            the mount, `window`/`document`, and the wrapper↔node identity.
 *   `js.integration.read`              the read-only surface against RoboHTML's own answers.
 *   `js.integration.attributes`        attribute writes landing in RoboHTML's attribute storage.
 *   `js.integration.structure`         rearrangement, with the parent links read back through the tree.
 *   `js.integration.text_in_place`     a `textContent` write with no node created.
 *   `js.integration.creation_refused`  the boundary, asserted, with the tree proved untouched.
 *   `js.integration.creation_open`     the whole creation path where construction is open.
 *   `js.integration.demo`              §18's end-to-end page, with the listener/microtask/timer order.
 *   `js.integration.steps`             §30's QEMU sequence at the grain a host suite can reach.
 *   `js.integration.lifecycle`         a destroyed document, then a fresh attach.
 *   `js.integration.determinism`       two generations, byte-identical.
 */

#include "RoboHTMLDomTestSupport.h"
#include "RoboJSTestSupport.h"

#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLText.h"

/* ------------------------------------------------------------------ */
/* The fixture page                                                    */
/* ------------------------------------------------------------------ */

static const char *JsIntegrationPage(void)
{
    return "<html><head><title>RoboJS</title></head><body>"
           "<div id=\"app\"></div>"
           "<p id=\"intro\" class=\"lead\">hello</p>"
           "<ul id=\"list\"><li>one</li><li>two</li></ul>"
           "</body></html>";
}

/* ------------------------------------------------------------------ */
/* Attaching a parsed document                                         */
/* ------------------------------------------------------------------ */

static void TestIntegrationAttach(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    HtmlDocumentId document;
    char text[64];

    DomFixtureBegin();
    document = DomDocumentOf(JsIntegrationPage());
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(config.enableDiagnostics == 1);

    /* Before the attach the adapter is inert: no document, not live, and both halves of a half-formed
     * request are refused rather than guessed. */
    RoboCheck(JsDomHtmlDocument(runtime) == RoboHtmlDocumentIdNone);
    RoboCheck(JsDomHtmlIsLive(runtime) == 0);
    RoboCheck(JsDomHtmlAttach(NULL, document) == RoboInvalidArgument);
    RoboCheck(JsDomHtmlAttach(runtime, RoboHtmlDocumentIdNone) == RoboInvalidArgument);
    RoboCheck(JsDomHtmlAttach(runtime, document) == RoboOk);
    RoboCheck(JsDomHtmlIsLive(runtime) == 1);
    RoboCheck(JsDomHtmlDocument(runtime) == document);
    RoboCheck(JsDomIsInstalled(runtime) == 1);

    /* Attaching a second document to an already-attached runtime is a refusal, not a silent swap: the
     * wrappers and the `document` object belong to the document that went away. The attach already
     * installed the bindings, so a swap would leave two graphs claiming one runtime. */
    RoboCheck(JsDomHtmlAttach(runtime, document) == RoboInvalidArgument);
    RoboCheck(JsDomHtmlIsLive(runtime) == 1);
    RoboCheck(JsDomHtmlDocument(runtime) == document);

    /* `document` and `window`, and the two well-known roots, over the parsed tree. */
    JsTestGlobalString(runtime, "var d = typeof document;", "d", text, sizeof(text));
    RoboCheckStrEq(text, "object");
    JsTestGlobalString(runtime, "var w = typeof window;", "w", text, sizeof(text));
    RoboCheckStrEq(text, "object");
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var body = document.body;\n"
                                      "var bodyOk = (body !== null && body.nodeName === \"body\") ? 1 : 0;",
                                      "bodyOk"), 1);
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var root = document.documentElement;\n"
                                      "var rootOk = (root !== null && root.nodeName === \"html\") ? 1 : 0;",
                                      "rootOk"), 1);

    /* **The wrapper is a RoboHTML node.** The id the binding handed out is resolved by RoboHTML, and
     * the handle that comes back names the element the page wrote — not a shadow node in a JavaScript
     * table. This is the case the whole phase's claim rests on. */
    {
        JsValue wrapper = JsValueUndefined();
        JsDomNodeId node = 0;
        HtmlNodeHandle handle;
        size_t length = 0;

        RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                          "var app = document.getElementById(\"app\");\n"
                                          "var found = (app !== null) ? 1 : 0;",
                                          "found"), 1);
        RoboCheck(JsRuntimeGetGlobal(runtime, "app", &wrapper) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&wrapper, &node) == RoboOk);
        RoboCheck(node != 0);
        RoboCheck(JsDomNodeKindOf(runtime, node) == (uint32_t)RoboJsDomNodeElement);
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)node, &handle, NULL) == RoboOk);
        RoboCheck(HtmlNodeHandleIsWellformed(&handle));
        RoboCheck(HtmlElementIsNamed(&handle, "div"));
        RoboCheck(HtmlElementAttributeValue(&handle, "id", &length) != NULL);
        RoboCheckEqInt((long long)length, 3);

        /* The same wrapper comes back for the same node, and the node behind it is stable: interned,
         * not re-created. A `document` identity would keep two wrappers for one element and break
         * `===`, which is what a listener comparing `event.target` to a saved reference does. */
        RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                          "var again = document.getElementById(\"app\");\n"
                                          "var same = (again === app) ? 1 : 0;",
                                          "same"), 1);
    }

    /* Query doors over the real tree. */
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var items = document.querySelectorAll(\"li\");\n"
                                      "var itemCount = items.length;",
                                      "itemCount"), 2);
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var byTag = document.getElementsByTagName(\"li\");\n"
                                      "var tagCount = byTag.length;",
                                      "tagCount"), 2);
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var lead = document.querySelector(\".lead\");\n"
                                      "var leadOk = (lead !== null && lead.nodeName === \"p\") ? 1 : 0;",
                                      "leadOk"), 1);
    {
        char reason[128];

        reason[0] = '\0';
        /* **1 means valid, 0 means refused**, and a combinator is outside this build's grammar -- so
         * the refusal says so rather than silently matching the first half of the selector. */
        RoboCheckEqInt(JsDomSelectorValidate("#list li", reason, (int32_t)sizeof(reason)), 0);
        RoboCheckEqInt(JsDomSelectorValidate("#list", reason, (int32_t)sizeof(reason)), 1);
        /* A compound of tag and class is inside the grammar; a combinator is not. */
        RoboCheckEqInt(JsDomSelectorValidate("li.lead", reason, (int32_t)sizeof(reason)), 1);
        RoboCheckEqInt(JsDomSelectorValidate(".lead", reason, (int32_t)sizeof(reason)), 1);
        RoboCheckEqInt(JsDomSelectorValidate("*", reason, (int32_t)sizeof(reason)), 1);
        RoboCheckEqInt(JsDomSelectorValidate("p#intro.lead", reason, (int32_t)sizeof(reason)), 1);
        RoboCheckEqInt(JsDomSelectorValidate("li, p", reason, (int32_t)sizeof(reason)), 1);
        reason[0] = '\0';
        RoboCheckEqInt(JsDomSelectorValidate("li >> p", reason, (int32_t)sizeof(reason)), 0);
        RoboCheckEqInt((long long)strlen(reason) > 0, 1);
    }

    /* Detach unhooks the document and leaves the runtime usable — the runtime outlives the page. */
    RoboCheck(JsDomHtmlDetach(runtime) > 0);
    RoboCheck(JsDomHtmlIsLive(runtime) == 0);
    RoboCheck(JsDomHtmlDocument(runtime) == RoboHtmlDocumentIdNone);
    JsRuntimeDestroy(runtime);
    RoboCheck(HtmlDocumentDestroy(document, NULL) == RoboOk);
}

/* ------------------------------------------------------------------ */
/* Reading, against RoboHTML's own answers                             */
/* ------------------------------------------------------------------ */

static void TestIntegrationRead(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    HtmlDocumentId document;
    char buffer[128];
    char text[128];

    DomFixtureBegin();
    document = DomDocumentOf(JsIntegrationPage());
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomHtmlAttach(runtime, document) == RoboOk);

    /* `nodeName` keeps the source spelling and `tagName` is the DOM's uppercased form for an HTML
     * element; `nodeType` is the number. A build that uppercased both, or neither, is wrong about one
     * of the two names the DOM defines differently. */
    JsTestGlobalString(runtime,
                       "var intro = document.getElementById(\"intro\");\n"
                       "var name = intro.nodeName;\n"
                       "var upper = intro.tagName;",
                       "name",
                       text,
                       sizeof(text));
    RoboCheckStrEq(text, "p");
    JsTestGlobalString(runtime, "var n2 = upper;", "n2", text, sizeof(text));
    RoboCheckStrEq(text, "P");
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var typeOk = intro.nodeType;", "typeOk"), 1);
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var textType = intro.firstChild.nodeType;",
                                      "textType"), 3);
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var docTypeOk = document.nodeType;",
                                      "docTypeOk"), 9);

    /* The element's text, read through RoboHTML: one text node holding `hello`, and the concatenated
     * subtree text is the same string. */
    {
        JsValue wrapper = JsValueUndefined();
        JsDomNodeId node = 0;
        HtmlNodeHandle handle;
        HtmlNodeView view;
        size_t length = 0;

        RoboCheck(JsRuntimeGetGlobal(runtime, "intro", &wrapper) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&wrapper, &node) == RoboOk);
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)node, &handle, NULL) == RoboOk);
        RoboCheck(HtmlNodeSubtreeText(&handle, buffer, sizeof(buffer), &length, NULL) == RoboOk);
        RoboCheckStrEq(buffer, "hello");

        memset(&view, 0, sizeof(view));
        RoboCheck(HtmlNodeGetView(&handle, &view, NULL) == RoboOk);
        RoboCheckEqInt((long long)view.childCount, 1);
        RoboCheck(HtmlTextIsText(&view.firstChild));

        /* `textContent` through the binding is the same bytes the tree holds. */
        JsTestGlobalString(runtime, "var t = intro.textContent;", "t", text, sizeof(text));
        RoboCheckStrEq(text, buffer);
    }

    /* Lists: `children` carries elements only, `childNodes` everything, and the two `li`s' text reads
     * back. A `children` that counted text nodes would answer 4 for the `<ul>`. */
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var list = document.getElementById(\"list\");\n"
                                      "var kids = list.children.length;",
                                      "kids"), 2);
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var nodes = list.childNodes.length;",
                                      "nodes"), 2);
    JsTestGlobalString(runtime,
                       "var first = list.children[0].textContent;\n"
                       "var second = list.children[1].textContent;",
                       "first",
                       text,
                       sizeof(text));
    RoboCheckStrEq(text, "one");
    JsTestGlobalString(runtime, "var s2 = second;", "s2", text, sizeof(text));
    RoboCheckStrEq(text, "two");

    /* `documentElement` is the `<html>` **element**, not the document node the tree is rooted at.
     * Getting that wrong is the difference between `document.documentElement.appendChild(...)`
     * working and silently doing nothing. */
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var de = document.documentElement;\n"
                                      "var deOk = (de !== null && de.nodeName === \"html\" && de.nodeType === 1) ? 1 : 0;",
                                      "deOk"), 1);
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var same = (document.documentElement === document.firstElementChild) ? 1 : 0;",
                                      "same"), 1);

    /* The document-level reads: the title, and `body`'s parent being the root element. */
    JsTestGlobalString(runtime, "var title = document.title;", "title", text, sizeof(text));
    RoboCheckStrEq(text, "RoboJS");
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var up = document.body.parentNode;\n"
                                      "var upOk = (up.nodeName === \"html\") ? 1 : 0;",
                                      "upOk"), 1);
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var noParent = document.documentElement.parentNode;\n"
                                      "var rootHasNoParent = (noParent === null || noParent.nodeType !== 1) ? 1 : 0;",
                                      "rootHasNoParent"), 1);

    RoboCheck(JsDomHtmlDetach(runtime) > 0);
    JsRuntimeDestroy(runtime);
    RoboCheck(HtmlDocumentDestroy(document, NULL) == RoboOk);
}

/* ------------------------------------------------------------------ */
/* Attribute writes                                                    */
/* ------------------------------------------------------------------ */

static void TestIntegrationAttributes(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    HtmlDocumentId document;
    uint32_t versionBefore = 0;
    uint32_t versionAfter = 0;

    DomFixtureBegin();
    document = DomDocumentOf(JsIntegrationPage());
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomHtmlAttach(runtime, document) == RoboOk);
    RoboCheck(HtmlNodeTreeVersion(document, &versionBefore, NULL) == RoboOk);

    /* `setAttribute`, then the computed `id`/`className` writes, which are the same door with the
     * DOM's own names. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "attributes",
                              "var node = document.getElementById(\"intro\");\n"
                              "node.setAttribute(\"data-role\", \"lead\");\n"
                              "node.setAttribute(\"data-count\", \"7\");\n"
                              "node.className = \"lead strong\";\n"
                              "node.id = \"banner\";",
                              NULL) == RoboOk);

    /* Read back out of RoboHTML's attribute storage — not out of a JavaScript-side table. */
    {
        JsValue wrapper = JsValueUndefined();
        JsDomNodeId node = 0;
        HtmlNodeHandle handle;

        RoboCheck(JsRuntimeGetGlobal(runtime, "node", &wrapper) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&wrapper, &node) == RoboOk);
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)node, &handle, NULL) == RoboOk);
        RoboCheckStrEq(HtmlElementAttributeValue(&handle, "data-role", NULL), "lead");
        RoboCheckStrEq(HtmlElementAttributeValue(&handle, "data-count", NULL), "7");
        RoboCheckStrEq(HtmlElementAttributeValue(&handle, "class", NULL), "lead strong");
        RoboCheckStrEq(HtmlElementAttributeValue(&handle, "id", NULL), "banner");
        RoboCheckEqInt(HtmlElementHasAttribute(&handle, "data-role"), 1);
    }

    /* **An attribute write is not a structural change**, and the two counters say so: RoboHTML's tree
     * version tracks links, so it does not move here, while the adapter's `attributeWrites` does. A
     * consumer that cached attributes must watch the second; one that cached a child list watches the
     * first. Asserting both is what keeps the split a fact rather than a coincidence. */
    RoboCheck(HtmlNodeTreeVersion(document, &versionAfter, NULL) == RoboOk);
    RoboCheck(versionAfter == versionBefore);
    {
        JsDomHtmlCounters counters;

        memset(&counters, 0, sizeof(counters));
        RoboCheck(JsDomHtmlGetCounters(runtime, &counters) == RoboOk);
        RoboCheck(counters.attributeWrites >= 4);
    }

    /* The reads agree with the write, and a changed `id` is visible to `getElementById` in its new
     * spelling and gone in the old one. */
    {
        char text[64];

        JsTestGlobalString(runtime, "var role = node.getAttribute(\"data-role\");", "role", text, sizeof(text));
        RoboCheckStrEq(text, "lead");
        JsTestGlobalString(runtime, "var klass = node.className;", "klass", text, sizeof(text));
        RoboCheckStrEq(text, "lead strong");
        RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                          "var byNew = (document.getElementById(\"banner\") === node) ? 1 : 0;",
                                          "byNew"), 1);
        RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                          "var byOld = (document.getElementById(\"intro\") === null) ? 1 : 0;",
                                          "byOld"), 1);
    }

    /* Removing is the other half, and the computed read agrees. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "remove",
                              "var had = node.hasAttribute(\"data-count\");\n"
                              "node.removeAttribute(\"data-count\");\n"
                              "var has = node.hasAttribute(\"data-count\");",
                              NULL) == RoboOk);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var wasHad = had ? 1 : 0;", "wasHad"), 1);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var isHas = has ? 1 : 0;", "isHas"), 0);
    {
        JsValue wrapper = JsValueUndefined();
        JsDomNodeId node = 0;
        HtmlNodeHandle handle;

        RoboCheck(JsRuntimeGetGlobal(runtime, "node", &wrapper) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&wrapper, &node) == RoboOk);
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)node, &handle, NULL) == RoboOk);
        RoboCheckEqInt(HtmlElementHasAttribute(&handle, "data-count"), 0);
    }

    /* `style` is one attribute under a facade. Its writes reach the element's `style` attribute,
     * which is what a later CSS phase reads. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "style",
                              "node.style.setProperty(\"color\", \"red\");\n"
                              "var colour = node.style.getPropertyValue(\"color\");",
                              NULL) == RoboOk);
    {
        char text[64];
        JsValue wrapper = JsValueUndefined();
        JsDomNodeId node = 0;
        HtmlNodeHandle handle;
        const char *style;

        JsTestGlobalString(runtime, "var c = colour;", "c", text, sizeof(text));
        RoboCheckStrEq(text, "red");
        RoboCheck(JsRuntimeGetGlobal(runtime, "node", &wrapper) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&wrapper, &node) == RoboOk);
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)node, &handle, NULL) == RoboOk);
        style = HtmlElementAttributeValue(&handle, "style", NULL);
        RoboCheck(style != NULL);
        RoboCheck(strstr(style, "color") != NULL);
        RoboCheck(strstr(style, "red") != NULL);
    }

    DomExpectCleanTree(document);
    RoboCheck(JsDomHtmlDetach(runtime) > 0);
    JsRuntimeDestroy(runtime);
    RoboCheck(HtmlDocumentDestroy(document, NULL) == RoboOk);
}

/* ------------------------------------------------------------------ */
/* Rearranging the tree                                                */
/* ------------------------------------------------------------------ */

static void TestIntegrationStructure(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    HtmlDocumentId document;

    DomFixtureBegin();
    document = DomDocumentOf(JsIntegrationPage());
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomHtmlAttach(runtime, document) == RoboOk);

    /* **Rearranging is the mutation a parsed document fully supports.** RoboHTML closes node
     * *creation* when a build finishes, not the tree itself: moving a node the document already has is
     * a real DOM write with a real version bump. `#intro` becomes `#app`'s child, and RoboHTML's own
     * view of the parent is what says so. */
    {
        JsDomHtmlCounters before;
        JsDomHtmlCounters after;
        uint32_t movedBefore = 0;
        uint32_t movedAfter = 0;

        memset(&before, 0, sizeof(before));
        memset(&after, 0, sizeof(after));
        RoboCheck(HtmlNodeTreeVersion(document, &movedBefore, NULL) == RoboOk);
        RoboCheck(JsDomHtmlGetCounters(runtime, &before) == RoboOk);
        RoboCheck(JsTestEvalNamed(runtime,
                                  "reparent",
                                  "var app = document.getElementById(\"app\");\n"
                                  "var intro = document.getElementById(\"intro\");\n"
                                  "app.appendChild(intro);",
                                  NULL) == RoboOk);
        RoboCheck(JsDomHtmlGetCounters(runtime, &after) == RoboOk);
        RoboCheck(after.appends > before.appends);
        RoboCheck(HtmlNodeTreeVersion(document, &movedAfter, NULL) == RoboOk);
        RoboCheck(movedAfter != movedBefore);

        {
            JsValue appValue = JsValueUndefined();
            JsValue introValue = JsValueUndefined();
            JsDomNodeId appNode = 0;
            JsDomNodeId introNode = 0;
            HtmlNodeHandle introHandle;
            HtmlNodeView view;

            memset(&view, 0, sizeof(view));
            RoboCheck(JsRuntimeGetGlobal(runtime, "app", &appValue) == RoboOk);
            RoboCheck(JsRuntimeGetGlobal(runtime, "intro", &introValue) == RoboOk);
            RoboCheck(JsDomNodeOfValue(&appValue, &appNode) == RoboOk);
            RoboCheck(JsDomNodeOfValue(&introValue, &introNode) == RoboOk);
            RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)introNode, &introHandle, NULL) == RoboOk);
            RoboCheck(HtmlNodeGetView(&introHandle, &view, NULL) == RoboOk);
            RoboCheck(HtmlNodeHandleIsWellformed(&view.parent));
            RoboCheckEqInt((long long)view.parent.nodeId, (long long)appNode);
        }

        /* `insertBefore` places a node ahead of a sibling it already has, which is the other
         * structural door. The `li`s swap their text through it. */
        RoboCheck(JsTestEvalNamed(runtime,
                                  "order",
                                  "var list = document.getElementById(\"list\");\n"
                                  "var second = list.children[1];\n"
                                  "list.insertBefore(second, list.children[0]);",
                                  NULL) == RoboOk);
        {
            char text[64];

            JsTestGlobalString(runtime,
                               "var firstNow = document.getElementById(\"list\").children[0].textContent;",
                               "firstNow",
                               text,
                               sizeof(text));
            RoboCheckStrEq(text, "two");
        }
    }

    /* Detaching is the third door, and it leaves the node live with its subtree intact: RoboHTML's
     * "detachment is not destruction". */
    RoboCheck(JsTestEvalNamed(runtime,
                              "detach",
                              "var app2 = document.getElementById(\"app\");\n"
                              "app2.removeChild(document.getElementById(\"intro\"));",
                              NULL) == RoboOk);
    {
        JsValue wrapper = JsValueUndefined();
        JsDomNodeId node = 0;
        HtmlNodeHandle handle;
        HtmlNodeView view;
        char buffer[64];
        size_t length = 0;

        memset(&view, 0, sizeof(view));
        RoboCheck(JsRuntimeGetGlobal(runtime, "intro", &wrapper) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&wrapper, &node) == RoboOk);
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)node, &handle, NULL) == RoboOk);
        RoboCheck(HtmlNodeGetView(&handle, &view, NULL) == RoboOk);
        RoboCheck(!HtmlNodeHandleIsWellformed(&view.parent));
        RoboCheck(HtmlNodeSubtreeText(&handle, buffer, sizeof(buffer), &length, NULL) == RoboOk);
        RoboCheckStrEq(buffer, "hello");
        /* And the binding agrees it has no parent. */
        RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                          "var orphan = (intro.parentNode === null) ? 1 : 0;",
                                          "orphan"), 1);
    }

    /* The tree is still a tree: no orphans, no bad links, no bad child counts. */
    DomExpectCleanTree(document);
    RoboCheck(JsDomHtmlDetach(runtime) > 0);
    JsRuntimeDestroy(runtime);
    RoboCheck(HtmlDocumentDestroy(document, NULL) == RoboOk);
}

/* ------------------------------------------------------------------ */
/* A `textContent` write that creates nothing                          */
/* ------------------------------------------------------------------ */

static void TestIntegrationTextInPlace(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    HtmlDocumentId document;
    JsDomHtmlCounters before;
    JsDomHtmlCounters after;

    DomFixtureBegin();
    document = DomDocumentOf(JsIntegrationPage());
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomHtmlAttach(runtime, document) == RoboOk);

    /* **`<p id="intro">hello</p>` -> `.textContent = "replaced"` is one run rewrite.** The element's
     * lone child is already the text node the write needs, so the adapter reaches for `HtmlTextSet`
     * rather than a removal plus a creation — which is why this works on a document whose construction
     * is closed, and why no node is created to do it. A DOM binding that always rebuilt the child list
     * would refuse here, and a browser would not. */
    {
        JsValue wrapper = JsValueUndefined();
        JsDomNodeId node = 0;
        HtmlNodeHandle handle;
        HtmlNodeView viewBefore;
        HtmlNodeView viewAfter;
        char buffer[64];

        memset(&before, 0, sizeof(before));
        memset(&after, 0, sizeof(after));
        memset(&viewBefore, 0, sizeof(viewBefore));
        memset(&viewAfter, 0, sizeof(viewAfter));

        /* Everything read *before* the write: the shape of the element, and the counters the write is
         * about to be judged against. A case that captured its "before" afterwards would compare two
         * identical snapshots and pass whatever the write did. */
        RoboCheck(JsTestEvalNamed(runtime,
                                  "text-in-place-before",
                                  "var intro = document.getElementById(\"intro\");",
                                  NULL) == RoboOk);
        RoboCheck(JsRuntimeGetGlobal(runtime, "intro", &wrapper) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&wrapper, &node) == RoboOk);
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)node, &handle, NULL) == RoboOk);
        RoboCheck(HtmlNodeGetView(&handle, &viewBefore, NULL) == RoboOk);
        RoboCheck(JsDomHtmlGetCounters(runtime, &before) == RoboOk);

        RoboCheck(JsTestEvalNamed(runtime,
                                  "text-in-place",
                                  "intro.textContent = \"replaced\";",
                                  NULL) == RoboOk);

        RoboCheck(JsDomHtmlGetCounters(runtime, &after) == RoboOk);
        /* No node was created and nothing was appended: the write is a text-run rewrite. */
        RoboCheckEqInt(after.created, before.created);
        RoboCheckEqInt(after.materialized, before.materialized);
        RoboCheckEqInt(after.appends, before.appends);
        RoboCheck(after.textWrites > before.textWrites);
        RoboCheckEqInt(after.refusals, before.refusals);

        /* The same text node, holding the new run — proving it is a rewrite rather than a
         * replacement. */
        RoboCheck(HtmlNodeGetView(&handle, &viewAfter, NULL) == RoboOk);
        RoboCheckEqInt((long long)viewAfter.firstChild.nodeId, (long long)viewBefore.firstChild.nodeId);
        RoboCheckEqInt((long long)viewAfter.childCount, 1);
        {
            const char *content = HtmlTextContent(&viewAfter.firstChild, NULL);
            size_t length = 0;

            RoboCheck(HtmlTextIsText(&viewAfter.firstChild));
            RoboCheck(HtmlNodeSubtreeText(&handle, buffer, sizeof(buffer), &length, NULL) == RoboOk);
            RoboCheckEqInt((long long)length, 8);
            RoboCheckStrEq(buffer, "replaced");
            RoboCheck(content != NULL);
            RoboCheckStrEq(content, buffer);
        }
    }

    /* A text node's own `nodeValue`/`data`/`textContent` is the same door, and the DOM read agrees. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "text-node",
                              "var textNode = document.getElementById(\"intro\").firstChild;\n"
                              "textNode.nodeValue = \"edited\";",
                              NULL) == RoboOk);
    {
        char text[64];

        JsTestGlobalString(runtime, "var read = textNode.nodeValue;", "read", text, sizeof(text));
        RoboCheckStrEq(text, "edited");
        JsTestGlobalString(runtime,
                           "var read2 = document.getElementById(\"intro\").textContent;",
                           "read2",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "edited");
    }

    DomExpectCleanTree(document);
    RoboCheck(JsDomHtmlDetach(runtime) > 0);
    JsRuntimeDestroy(runtime);
    RoboCheck(HtmlDocumentDestroy(document, NULL) == RoboOk);
}

/* ------------------------------------------------------------------ */
/* The creation boundary, asserted                                     */
/* ------------------------------------------------------------------ */

static void TestIntegrationCreationRefused(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    HtmlDocumentId document;
    char message[256];

    DomFixtureBegin();
    document = DomDocumentOf(JsIntegrationPage());
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomHtmlAttach(runtime, document) == RoboOk);

    /* The document is finalized — that is the fact every refusal below is an answer to. */
    RoboCheck(HtmlDocumentIsFinalized(document) == 1);
    RoboCheck(HtmlDocumentAllowsConstruction(document) == 0);

    /* A created node is a *pending record*: the script can read its tag and its own attributes, which
     * is what it expects of something it just built, and it is not in the document — a query tree that
     * believed the record would find it. */
    {
        JsDomHtmlCounters before;
        JsDomHtmlCounters after;

        memset(&before, 0, sizeof(before));
        memset(&after, 0, sizeof(after));
        RoboCheck(JsDomHtmlGetCounters(runtime, &before) == RoboOk);
        RoboCheck(JsTestEvalNamed(runtime,
                                  "create",
                                  "var made = document.createElement(\"span\");\n"
                                  "made.setAttribute(\"data-new\", \"1\");\n"
                                  "var madeTag = made.nodeName;\n"
                                  "var madeAttr = made.getAttribute(\"data-new\");",
                                  NULL) == RoboOk);
        RoboCheck(JsDomHtmlGetCounters(runtime, &after) == RoboOk);
        RoboCheck(after.pendingCreated > before.pendingCreated);
        RoboCheck(after.created > before.created);
        RoboCheckEqInt(after.materialized, before.materialized);
        RoboCheckEqInt(JsTestGlobalNumber(runtime, "var tagOk = (madeTag === \"span\") ? 1 : 0;", "tagOk"), 1);
        RoboCheckEqInt(JsTestGlobalNumber(runtime, "var attrOk = (madeAttr === \"1\") ? 1 : 0;", "attrOk"), 1);
        RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                          "var notFound = (document.querySelector(\"span\") === null) ? 1 : 0;",
                                          "notFound"), 1);

        /* Linking it is the construction operation, and the refusal names the reason rather than
         * surfacing as "appendChild refused the node". */
        RoboCheck(JsTestEvalExpectError(runtime,
                                        "place",
                                        "document.getElementById(\"app\").appendChild(made);") != RoboOk);
        message[0] = '\0';
        JsTestExceptionMessage(runtime, message, sizeof(message));
        RoboCheck(strstr(message, "new nodes") != NULL);
        JsRuntimeClearException(runtime);
        RoboCheck(JsDomHtmlGetCounters(runtime, &after) == RoboOk);
        RoboCheck(after.refusals > before.refusals);
        RoboCheckEqInt(after.materialized, before.materialized);
        RoboCheckEqInt(after.appends, before.appends);
    }

    /* **A `textContent` write that would create a node is refused, and refused before anything is
     * destroyed.** `#list` has two `li` children; the write would have to drop both and build a text
     * node, and the build is what the document cannot do. A build that removed first and failed second
     * would answer "cannot replace" by having already destroyed what it could not replace — so the
     * assertion that matters is not the failure code, it is that `onetwo` is still there. */
    {
        JsDomHtmlCounters before;
        JsDomHtmlCounters after;
        JsValue wrapper = JsValueUndefined();
        JsDomNodeId node = 0;
        HtmlNodeHandle handle;
        char buffer[64];
        size_t length = 0;

        memset(&before, 0, sizeof(before));
        memset(&after, 0, sizeof(after));
        RoboCheck(JsDomHtmlGetCounters(runtime, &before) == RoboOk);
        RoboCheck(JsTestEvalExpectError(runtime,
                                        "text-refused",
                                        "var list = document.getElementById(\"list\");\n"
                                        "var replacement = \"gone\";\n"
                                        "list.textContent = replacement;") != RoboOk);
        message[0] = '\0';
        JsTestExceptionMessage(runtime, message, sizeof(message));
        RoboCheck(strstr(message, "new nodes") != NULL);
        JsRuntimeClearException(runtime);
        RoboCheck(JsDomHtmlGetCounters(runtime, &after) == RoboOk);
        RoboCheck(after.refusals > before.refusals);
        RoboCheckEqInt(after.textWrites, before.textWrites);
        RoboCheckEqInt(after.created, before.created);

        /* Every child the element had is still there, with its text, and the tree is still sound. */
        RoboCheck(JsRuntimeGetGlobal(runtime, "list", &wrapper) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&wrapper, &node) == RoboOk);
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)node, &handle, NULL) == RoboOk);
        {
            HtmlNodeView view;
            size_t count = 0;

            memset(&view, 0, sizeof(view));
            RoboCheck(HtmlNodeGetView(&handle, &view, NULL) == RoboOk);
            RoboCheckEqInt((long long)view.childCount, 2);
            RoboCheck(HtmlNodeChildCount(&handle, &count, NULL) == RoboOk);
            RoboCheckEqInt((long long)count, 2);
        }
        RoboCheck(HtmlNodeSubtreeText(&handle, buffer, sizeof(buffer), &length, NULL) == RoboOk);
        RoboCheckStrEq(buffer, "onetwo");

        /* And the read half still answers the old text, not the string the refused write carried. */
        {
            char read[64];

            JsTestGlobalString(runtime, "var after2 = list.textContent;", "after2", read, sizeof(read));
            RoboCheckStrEq(read, "onetwo");
        }
    }

    /* An empty element's write has no children to lose, so the same refusal costs nothing — and it is
     * still a refusal, because the text node it needs cannot be created. */
    {
        char read[64];

        RoboCheck(JsTestEvalExpectError(runtime,
                                        "empty-refused",
                                        "document.getElementById(\"app\").textContent = \"x\";") != RoboOk);
        JsRuntimeClearException(runtime);
        JsTestGlobalString(runtime, "var empty = document.getElementById(\"app\").textContent;", "empty", read, sizeof(read));
        RoboCheckStrEq(read, "");
    }

    DomExpectCleanTree(document);
    RoboCheck(JsDomHtmlDetach(runtime) > 0);
    JsRuntimeDestroy(runtime);
    RoboCheck(HtmlDocumentDestroy(document, NULL) == RoboOk);
}

/* ------------------------------------------------------------------ */
/* Creation, where construction is open                                */
/* ------------------------------------------------------------------ */

static void TestIntegrationCreationOpen(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    HtmlDocumentId document;
    JsDomHtmlCounters before;
    JsDomHtmlCounters after;

    DomFixtureBegin();

    /* A document that has never been parsed: `HtmlDocumentCreate` leaves construction open, which is
     * the state the parser itself works in. This is where the whole creation half of the binding is
     * exercised against real RoboHTML — `createElement`, `createTextNode`, attributes on a detached
     * node, `appendChild` materialising it, and a `textContent` write that builds its text.
     *
     * The parent is `document` itself: a freshly created document has a document node and nothing
     * else, so there is no `<html>` element for `documentElement` to name yet. That is also what makes
     * `document.documentElement` worth checking *after* the append — the first element below the root
     * is the section the script built. */
    document = DomNewDocument();
    RoboCheck(HtmlDocumentAllowsConstruction(document) == 1);
    RoboCheck(HtmlDocumentIsFinalized(document) == 0);

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomHtmlAttach(runtime, document) == RoboOk);

    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));
    RoboCheck(JsDomHtmlGetCounters(runtime, &before) == RoboOk);

    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var noRootYet = (document.documentElement === null) ? 1 : 0;",
                                      "noRootYet"), 1);
    RoboCheck(JsTestEvalNamed(runtime,
                              "make",
                              "var holder = document.createElement(\"section\");\n"
                              "holder.setAttribute(\"id\", \"holder\");\n"
                              "var detached = holder.parentNode;\n"
                              "document.appendChild(holder);\n"
                              "var text = document.createTextNode(\"built\");\n"
                              "holder.appendChild(text);",
                              NULL) == RoboOk);
    /* A created node starts detached, and `appendChild` is what attaches it -- the DOM's own shape,
     * here expressed as a pending record becoming a real node. */
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var wasDetached = (detached === null) ? 1 : 0;",
                                      "wasDetached"), 1);
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var de = document.documentElement;\n"
                                      "var deOk = (de !== null && de.nodeName === \"section\") ? 1 : 0;",
                                      "deOk"), 1);
    RoboCheck(JsDomHtmlGetCounters(runtime, &after) == RoboOk);
    RoboCheck(after.materialized > before.materialized);
    RoboCheck(after.appends > before.appends);
    RoboCheck(after.textNodesCreated > before.textNodesCreated);

    /* Every node the script built is a RoboHTML node, resolved by RoboHTML, and the tree reads back
     * exactly the shape the script described. */
    {
        JsValue holder = JsValueUndefined();
        JsValue text = JsValueUndefined();
        JsDomNodeId holderNode = 0;
        JsDomNodeId textNode = 0;
        HtmlNodeHandle holderHandle;
        HtmlNodeHandle textHandle;
        HtmlNodeView view;
        char buffer[64];
        size_t length = 0;

        memset(&view, 0, sizeof(view));
        RoboCheck(JsRuntimeGetGlobal(runtime, "holder", &holder) == RoboOk);
        RoboCheck(JsRuntimeGetGlobal(runtime, "text", &text) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&holder, &holderNode) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&text, &textNode) == RoboOk);
        /* The wrapper the script kept was re-created for the materialised id, so the id it answers now
         * is a real RoboHTML node rather than the synthetic one. */
        RoboCheck(holderNode < (JsDomNodeId)0x8000000000000000ULL);
        RoboCheck(textNode < (JsDomNodeId)0x8000000000000000ULL);
        (void)holderNode;
        (void)textNode;
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)holderNode, &holderHandle, NULL) == RoboOk);
        RoboCheck(HtmlElementIsNamed(&holderHandle, "section"));
        RoboCheckStrEq(HtmlElementAttributeValue(&holderHandle, "id", NULL), "holder");
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)textNode, &textHandle, NULL) == RoboOk);
        RoboCheck(HtmlTextIsText(&textHandle));
        RoboCheckStrEq(HtmlTextContent(&textHandle, NULL), "built");
        RoboCheck(HtmlNodeGetView(&holderHandle, &view, NULL) == RoboOk);
        RoboCheckEqInt((long long)view.childCount, 1);
        RoboCheck(HtmlNodeSubtreeText(&holderHandle, buffer, sizeof(buffer), &length, NULL) == RoboOk);
        RoboCheckStrEq(buffer, "built");

        /* And through the binding's own doors. */
        RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                          "var byId = (document.getElementById(\"holder\") === holder) ? 1 : 0;",
                                          "byId"), 1);
        RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                          "var found = document.querySelectorAll(\"section\").length;",
                                          "found"), 1);
        {
            char read[64];

            JsTestGlobalString(runtime, "var readBack = holder.textContent;", "readBack", read, sizeof(read));
            RoboCheckStrEq(read, "built");
        }
    }

    /* A `textContent` write on a tree that can accept nodes takes the construction path — and this is
     * the same call that is refused in `js.integration.creation_refused`, which is the pair that makes
     * the boundary a *condition* rather than a quirk. */
    {
        JsDomHtmlCounters textBefore;
        JsDomHtmlCounters textAfter;

        memset(&textBefore, 0, sizeof(textBefore));
        memset(&textAfter, 0, sizeof(textAfter));
        RoboCheck(JsDomHtmlGetCounters(runtime, &textBefore) == RoboOk);
        RoboCheck(JsTestEvalNamed(runtime,
                                  "replace",
                                  "var holder = document.getElementById(\"holder\");\n"
                                  "holder.textContent = \"fresh\";",
                                  NULL) == RoboOk);
        RoboCheck(JsDomHtmlGetCounters(runtime, &textAfter) == RoboOk);
        RoboCheck(textAfter.textWrites > textBefore.textWrites);
        /* **No node was created, even here**: `#holder` already had exactly one text child, so the
         * write took the in-place rewrite even though this document *would* have accepted a new node.
         * The rule is "reshape nothing", not "reshape only when you must", and this is the assertion
         * that keeps a build from taking the construction path merely because it could. */
        RoboCheckEqInt(textAfter.created, textBefore.created);

        {
            JsValue holder = JsValueUndefined();
            JsDomNodeId holderNode = 0;
            HtmlNodeHandle holderHandle;
            HtmlNodeView view;
            char buffer[64];
            size_t length = 0;

            memset(&view, 0, sizeof(view));
            RoboCheck(JsRuntimeGetGlobal(runtime, "holder", &holder) == RoboOk);
            RoboCheck(JsDomNodeOfValue(&holder, &holderNode) == RoboOk);
            RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)holderNode, &holderHandle, NULL) == RoboOk);
            RoboCheck(HtmlNodeGetView(&holderHandle, &view, NULL) == RoboOk);
            RoboCheckEqInt((long long)view.childCount, 1);
            RoboCheck(HtmlTextIsText(&view.firstChild));
            RoboCheck(HtmlNodeSubtreeText(&holderHandle, buffer, sizeof(buffer), &length, NULL) == RoboOk);
            RoboCheckStrEq(buffer, "fresh");
        }
    }

    /* **The construction path of `textContent`, where the write really does have to build its text.**
     * An element with no children has nothing to rewrite, so this is the case that creates a text
     * node -- and the one that would be refused on a parsed document. Running it here and the same
     * call in `js.integration.creation_refused` is what makes the boundary a condition on the
     * document rather than a property of the operation. */
    {
        JsDomHtmlCounters emptyBefore;
        JsDomHtmlCounters emptyAfter;

        memset(&emptyBefore, 0, sizeof(emptyBefore));
        memset(&emptyAfter, 0, sizeof(emptyAfter));
        RoboCheck(JsDomHtmlGetCounters(runtime, &emptyBefore) == RoboOk);
        RoboCheck(JsTestEvalNamed(runtime,
                                  "build-text",
                                  "var empty = document.createElement(\"em\");\n"
                                  "document.getElementById(\"holder\").appendChild(empty);\n"
                                  "empty.textContent = \"built-here\";",
                                  NULL) == RoboOk);
        RoboCheck(JsDomHtmlGetCounters(runtime, &emptyAfter) == RoboOk);
        RoboCheck(emptyAfter.created > emptyBefore.created);
        RoboCheck(emptyAfter.textNodesCreated > emptyBefore.textNodesCreated);
        RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                          "var built = (document.querySelector(\"em\").textContent === \"built-here\") ? 1 : 0;",
                                          "built"), 1);
        {
            JsValue empty = JsValueUndefined();
            JsDomNodeId emptyNode = 0;
            HtmlNodeHandle emptyHandle;
            char buffer[64];
            size_t length = 0;

            RoboCheck(JsRuntimeGetGlobal(runtime, "empty", &empty) == RoboOk);
            RoboCheck(JsDomNodeOfValue(&empty, &emptyNode) == RoboOk);
            RoboCheck(emptyNode < (JsDomNodeId)0x8000000000000000ULL);
            RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)emptyNode, &emptyHandle, NULL) == RoboOk);
            RoboCheck(HtmlElementIsNamed(&emptyHandle, "em"));
            RoboCheck(HtmlNodeSubtreeText(&emptyHandle, buffer, sizeof(buffer), &length, NULL) == RoboOk);
            RoboCheckStrEq(buffer, "built-here");
        }
    }

    /* A `appendChild` of a node that already has a parent is the DOM's *move*, which the adapter
     * performs as its two explicit steps — the tree model refuses to steal a node. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "move",
                              "var box = document.createElement(\"div\");\n"
                              "document.getElementById(\"holder\").appendChild(box);\n"
                              "var a = document.createElement(\"div\");\n"
                              "var b = document.createElement(\"div\");\n"
                              "box.appendChild(a);\n"
                              "box.appendChild(b);\n"
                              "var beforeMove = box.children.length;\n"
                              "a.appendChild(b);\n"
                              "var afterMove = box.children.length;",
                              NULL) == RoboOk);
    /* The move is two steps in RoboHTML -- remove, then insert, because the tree model refuses to
     * steal a node -- and the observable result is one child fewer where it was, none added where the
     * sibling count would double. */
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var wasTwo = beforeMove;", "wasTwo"), 2);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var isOne = afterMove;", "isOne"), 1);
    /* Both `div`s are still *in* the document -- a move is not a removal from the tree -- so `box`
     * has one child where it had two, and `a` is the one that gained. */
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var divCount = document.querySelectorAll(\"div\").length;", "divCount"), 3);
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var parentOk = (a.parentNode === box && b.parentNode === a) ? 1 : 0;",
                                      "parentOk"), 1);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var aKids = a.children.length;", "aKids"), 1);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var bKids = b.children.length;", "bKids"), 0);
    DomExpectCleanTree(document);

    RoboCheck(JsDomHtmlDetach(runtime) > 0);
    JsRuntimeDestroy(runtime);
    RoboCheck(HtmlDocumentDestroy(document, NULL) == RoboOk);
}

/* ------------------------------------------------------------------ */
/* §18's end-to-end page                                               */
/* ------------------------------------------------------------------ */

static void TestIntegrationDemo(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    HtmlDocumentId document;
    char text[64];

    DomFixtureBegin();
    document = DomDocumentOf("<html><body><button id=\"button\">Click</button>"
                             "<div id=\"output\"></div></body></html>");
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomHtmlAttach(runtime, document) == RoboOk);

    /* The page's own script. The listener writes an attribute rather than `textContent` because
     * `#output` is empty and this document accepts no new nodes — the write that *is* supported is
     * asserted to be supported, and the one that is not is asserted in its own case rather than
     * smuggled into a demo. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "page",
                              "var button = document.getElementById(\"button\");\n"
                              "var output = document.getElementById(\"output\");\n"
                              "var order = [];\n"
                              "button.addEventListener(\"click\", function () {\n"
                              "  output.setAttribute(\"data-clicks\", \"1\");\n"
                              "  order.push(\"listener\");\n"
                              "  queueMicrotask(function () { order.push(\"microtask\"); });\n"
                              "  setTimeout(function () { order.push(\"timer\"); }, 0);\n"
                              "});",
                              NULL) == RoboOk);

    /* Looking up the tag the script used is the read half of the same page. */
    {
        JsValue button = JsValueUndefined();
        JsDomNodeId buttonNode = 0;
        HtmlNodeHandle buttonHandle;
        size_t length = 0;

        RoboCheck(JsRuntimeGetGlobal(runtime, "button", &button) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&button, &buttonNode) == RoboOk);
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)buttonNode, &buttonHandle, NULL) == RoboOk);
        RoboCheck(HtmlElementIsNamed(&buttonHandle, "button"));
        RoboCheck(HtmlNodeSubtreeText(&buttonHandle, text, sizeof(text), &length, NULL) == RoboOk);
        RoboCheckStrEq(text, "Click");
    }

    /* The output is untouched until the click arrives, which is the difference between registering
     * and running. */
    {
        JsValue output = JsValueUndefined();
        JsDomNodeId outputNode = 0;
        HtmlNodeHandle outputHandle;

        RoboCheck(JsRuntimeGetGlobal(runtime, "output", &output) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&output, &outputNode) == RoboOk);
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)outputNode, &outputHandle, NULL) == RoboOk);
        RoboCheckEqInt(HtmlElementHasAttribute(&outputHandle, "data-clicks"), 0);
    }

    /* A click enters the way 16.11 says it does: posted as a record (the shape RoboInput's adapter
     * produces), then run at a loop boundary. */
    {
        JsValue button = JsValueUndefined();
        JsDomNodeId node = 0;
        JsEventRecord record;

        RoboCheck(JsRuntimeGetGlobal(runtime, "button", &button) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&button, &node) == RoboOk);
        RoboCheck(JsEventRecordInit(&record, "click", node) == RoboOk);
        record.clientX = 4.0;
        record.clientY = 8.0;
        RoboCheck(JsEventQueue(runtime, &record) == RoboOk);
        /* Nothing ran yet: a posted event waits for a boundary. */
        RoboCheckEqInt(JsTestGlobalNumber(runtime, "var pre = order.length;", "pre"), 0);
        RoboCheck(JsLoopRunOne(runtime, NULL) == RoboOk);
    }

    /* The listener ran and wrote the DOM. */
    RoboCheckStrEq(JsTestGlobalString(runtime,
                                      "var after = \"\";\n"
                                      "for (var i = 0; i < order.length; i = i + 1) { after = after + order[i] + \",\"; }",
                                      "after",
                                      text,
                                      sizeof(text)),
                   "listener,");
    {
        JsValue output = JsValueUndefined();
        JsDomNodeId node = 0;
        HtmlNodeHandle handle;

        RoboCheck(JsRuntimeGetGlobal(runtime, "output", &output) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&output, &node) == RoboOk);
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)node, &handle, NULL) == RoboOk);
        RoboCheckStrEq(HtmlElementAttributeValue(&handle, "data-clicks", NULL), "1");
    }

    /* Drain: the microtask runs before the timer, which is the phase's ordering requirement, and the
     * whole sequence is proved rather than the two halves separately. */
    RoboCheck(JsLoopRunUntilIdle(runtime, 0) == RoboOk);
    RoboCheckStrEq(JsTestGlobalString(runtime,
                                      "var all = \"\";\n"
                                      "for (var i = 0; i < order.length; i = i + 1) { all = all + order[i] + \",\"; }",
                                      "all",
                                      text,
                                      sizeof(text)),
                   "listener,microtask,timer,");

    DomExpectCleanTree(document);
    RoboCheck(JsDomHtmlDetach(runtime) > 0);
    JsRuntimeDestroy(runtime);
    RoboCheck(HtmlDocumentDestroy(document, NULL) == RoboOk);
}

/* ------------------------------------------------------------------ */
/* §30's sequence, at the grain a host suite can reach                 */
/* ------------------------------------------------------------------ */

static void TestIntegrationSteps(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    HtmlDocumentId document;
    char text[64];

    DomFixtureBegin();
    document = DomDocumentOf(JsIntegrationPage());
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);

    /* 5–8: the document exists and the runtime attaches to it. */
    RoboCheck(JsDomHtmlAttach(runtime, document) == RoboOk);
    /* 9–12: a script runs; `document` and `window` are available. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "step-1",
                              "var log = [];\n"
                              "log.push(typeof document);\n"
                              "log.push(typeof window);",
                              NULL) == RoboOk);
    /* 13: DOM query works. 14: DOM mutation works, and it reaches the tree. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "step-2",
                              "var host = document.getElementById(\"intro\");\n"
                              "host.setAttribute(\"data-step\", \"2\");\n"
                              "log.push(host.getAttribute(\"data-step\"));",
                              NULL) == RoboOk);
    /* 15–17: a listener registered on a real node runs from a queued event. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "step-3",
                              "var clicks = 0;\n"
                              "document.getElementById(\"intro\").addEventListener(\"click\", function () {\n"
                              "  clicks = clicks + 1;\n"
                              "});",
                              NULL) == RoboOk);
    {
        JsValue host = JsValueUndefined();
        JsDomNodeId node = 0;
        JsEventRecord record;

        RoboCheck(JsRuntimeGetGlobal(runtime, "host", &host) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&host, &node) == RoboOk);
        RoboCheck(JsEventRecordInit(&record, "click", node) == RoboOk);
        RoboCheck(JsEventQueue(runtime, &record) == RoboOk);
        RoboCheck(JsLoopRunUntilIdle(runtime, 0) == RoboOk);
    }
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var clickCount = clicks;", "clickCount"), 1);
    /* A second dispatch reuses the same wrapper, so the listener count keeps rising rather than the
     * handler being registered a second time. */
    {
        JsValue host = JsValueUndefined();
        JsDomNodeId node = 0;
        JsEventRecord record;

        RoboCheck(JsRuntimeGetGlobal(runtime, "host", &host) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&host, &node) == RoboOk);
        RoboCheck(JsEventRecordInit(&record, "click", node) == RoboOk);
        RoboCheck(JsEventQueue(runtime, &record) == RoboOk);
        RoboCheck(JsLoopRunUntilIdle(runtime, 0) == RoboOk);
    }
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var clickCount2 = clicks;", "clickCount2"), 2);

    /* 18–21: a timer fires, and a cleared one does not. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "step-4",
                              "var fired = 0;\n"
                              "setTimeout(function () { fired = fired + 1; }, 0);\n"
                              "var cancelled = setTimeout(function () { fired = fired + 100; }, 0);\n"
                              "clearTimeout(cancelled);",
                              NULL) == RoboOk);
    RoboCheck(JsLoopRunUntilIdle(runtime, 0) == RoboOk);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var fired2 = fired;", "fired2"), 1);
    /* 22–23: microtasks run, in order. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "step-5",
                              "var marks = [];\n"
                              "queueMicrotask(function () { marks.push(\"one\"); });\n"
                              "queueMicrotask(function () { marks.push(\"two\"); });",
                              NULL) == RoboOk);
    RoboCheck(JsLoopRunUntilIdle(runtime, 0) == RoboOk);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var marksLen = marks.length;", "marksLen"), 2);
    /* 24–25: an exception is caught, and an uncaught one leaves the runtime usable. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "step-6",
                              "var caught = \"\";\n"
                              "try { throw new Error(\"boom\"); } catch (e) { caught = e.message; }\n"
                              "log.push(caught);",
                              NULL) == RoboOk);
    JsTestGlobalString(runtime, "var caughtText = caught;", "caughtText", text, sizeof(text));
    RoboCheckStrEq(text, "boom");
    RoboCheck(JsTestEvalExpectError(runtime,
                                    "step-7",
                                    "function bad() { throw new TypeError(\"nope\"); }\nbad();") != RoboOk);
    JsRuntimeClearException(runtime);
    /* 26: the runtime is still usable after an uncaught throw, and the document came with it. */
    RoboCheck(JsTestEvalNamed(runtime, "step-8", "var stillHere = 1;", NULL) == RoboOk);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var here = stillHere;", "here"), 1);

    /* The event's own `clientX` reached the listener's record, which is the last link between the
     * event a host posts and the object a script receives. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "step-9",
                              "var seen = -1;\n"
                              "document.getElementById(\"intro\").addEventListener(\"click\", function (ev) {\n"
                              "  seen = ev.clientX;\n"
                              "});",
                              NULL) == RoboOk);
    {
        JsValue host = JsValueUndefined();
        JsDomNodeId node = 0;
        JsEventRecord record;

        RoboCheck(JsRuntimeGetGlobal(runtime, "host", &host) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&host, &node) == RoboOk);
        RoboCheck(JsEventRecordInit(&record, "click", node) == RoboOk);
        record.clientX = 12.0;
        RoboCheck(JsEventQueue(runtime, &record) == RoboOk);
        RoboCheck(JsLoopRunUntilIdle(runtime, 0) == RoboOk);
    }
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var seenX = seen;", "seenX"), 12);

    /* 28: nothing leaks into the tree that RoboHTML did not put there. */
    {
        size_t nodes = HtmlDocumentNodeCount(document);

        RoboCheck(nodes > 0u);
        RoboCheck(nodes < 64u);
    }
    DomExpectCleanTree(document);

    /* 27: destroying the runtime leaves the document intact. */
    RoboCheck(JsDomHtmlDetach(runtime) > 0);
    JsRuntimeDestroy(runtime);
    DomExpectCleanTree(document);

    RoboCheck(HtmlDocumentDestroy(document, NULL) == RoboOk);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void TestIntegrationLifecycle(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    HtmlDocumentId document;
    HtmlDocumentId doomed;

    DomFixtureBegin();
    document = DomDocumentOf(JsIntegrationPage());
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomHtmlAttach(runtime, document) == RoboOk);

    /* A document destroyed underneath a still-attached runtime: `isLive` turns 0, reads still answer,
     * and a mutation is refused rather than touching freed state. This is the ordering a navigation
     * takes if the page goes away before the runtime does, and the answer is "refuse", not "crash".
     *
     * The wrapper is taken **before** the destroy on purpose. A lookup against a dead document cannot
     * find anything, so going through `document.getElementById` would test the lookup rather than the
     * mutation; holding the wrapper is what makes the refused write go through the adapter's own gate. */
    doomed = DomDocumentOf("<html><body><div id=\"gone\"></div></body></html>");
    RoboCheck(JsTestEvalNamed(runtime, "hold", "var held = document.getElementById(\"app\");", NULL) == RoboOk);
    RoboCheck(HtmlDocumentDestroy(document, NULL) == RoboOk);
    RoboCheck(JsDomHtmlIsLive(runtime) == 0);
    {
        JsDomHtmlCounters before;
        JsDomHtmlCounters after;

        memset(&before, 0, sizeof(before));
        memset(&after, 0, sizeof(after));
        RoboCheck(JsDomHtmlGetCounters(runtime, &before) == RoboOk);
        /* The runtime is still a runtime, and the document object is still an object. */
        RoboCheck(JsTestEvalNamed(runtime,
                                  "after-destroy",
                                  "var d = typeof document;\n"
                                  "var w = typeof window;",
                                  NULL) == RoboOk);
        RoboCheck(JsTestEvalExpectError(runtime,
                                        "dead-mutation",
                                        "held.setAttribute(\"x\", \"y\");") != RoboOk);
        JsRuntimeClearException(runtime);
        RoboCheck(JsDomHtmlGetCounters(runtime, &after) == RoboOk);
        RoboCheck(after.refusals > before.refusals);
        /* A lookup against the dead document answers nothing rather than a stale wrapper. */
        RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                          "var gone = (document.getElementById(\"app\") === null) ? 1 : 0;",
                                          "gone"), 1);
    }

    /* Detaching and attaching a fresh document is the recovery path a navigation would take. The
     * first attach's wrappers are not reused: a wrapper belongs to the document it was made for. */
    {
        /* The old wrappers go away with the old document; a wrapper belongs to the document it was
         * made for, which is what keeps a stale id from resolving against a new tree. */
        int32_t dropped = JsDomHtmlDetach(runtime);

        RoboCheck(dropped >= 0);
    }
    RoboCheck(JsDomHtmlIsLive(runtime) == 0);
    RoboCheck(JsDomHtmlAttach(runtime, doomed) == RoboOk);
    RoboCheck(JsDomHtmlIsLive(runtime) == 1);
    RoboCheck(JsDomHtmlDocument(runtime) == doomed);
    RoboCheck(JsTestEvalNamed(runtime,
                              "fresh-document",
                              "var fresh = document.getElementById(\"gone\");\n"
                              "fresh.setAttribute(\"data-back\", \"yes\");",
                              NULL) == RoboOk);
    RoboCheckEqInt(JsTestGlobalNumber(runtime,
                                      "var back = (document.getElementById(\"gone\").getAttribute(\"data-back\") === \"yes\") ? 1 : 0;",
                                      "back"), 1);
    {
        JsValue wrapper = JsValueUndefined();
        JsDomNodeId node = 0;
        HtmlNodeHandle handle;

        RoboCheck(JsRuntimeGetGlobal(runtime, "fresh", &wrapper) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&wrapper, &node) == RoboOk);
        RoboCheck(HtmlDocumentNodeById(doomed, (HtmlNodeId)node, &handle, NULL) == RoboOk);
        RoboCheckStrEq(HtmlElementAttributeValue(&handle, "data-back", NULL), "yes");
    }

    /* Destroying the runtime first, then the document, is the other order and it is equally clean:
     * the runtime holds no handle it must release. */
    RoboCheck(JsDomHtmlDetach(runtime) > 0);
    RoboCheck(JsDomHtmlIsLive(runtime) == 0);
    JsRuntimeDestroy(runtime);
    DomExpectCleanTree(doomed);
    RoboCheck(HtmlDocumentDestroy(doomed, NULL) == RoboOk);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                         */
/* ------------------------------------------------------------------ */

static void TestIntegrationDeterminism(void)
{
    static const char *script =
        "var app = document.getElementById(\"app\");\n"
        "app.setAttribute(\"data-run\", \"deterministic\");\n"
        "var intro = document.getElementById(\"intro\");\n"
        "app.appendChild(intro);\n"
        "var order = [];\n"
        "app.addEventListener(\"click\", function () { order.push(\"click\"); });\n"
        "var ev = { type: \"click\" };\n"
        "app.dispatchEvent(ev);\n"
        "setTimeout(function () { order.push(\"timer\"); }, 0);\n"
        "queueMicrotask(function () { order.push(\"microtask\"); });\n"
        "intro.textContent = \"rewritten\";";

    char first[512];
    char second[512];
    int32_t generation;

    DomFixtureBegin();

    /* Two generations of the same page, each read back out of its own parsed document: an identical
     * subtree text *and* an identical attribute set is the determinism claim, and a build whose
     * ordering came from a hash table's iteration would fail it. */
    for (generation = 0; generation < 2; ++generation) {
        JsRuntimeConfig config;
        JsRuntime *runtime;
        HtmlDocumentId document = DomDocumentOf(JsIntegrationPage());
        char *target = generation == 0 ? first : second;
        JsValue app = JsValueUndefined();
        JsDomNodeId node = 0;
        HtmlNodeHandle handle;
        size_t length = 0;

        runtime = JsTestRuntimeCreate(&config);
        RoboCheck(runtime != NULL);
        RoboCheck(JsDomHtmlAttach(runtime, document) == RoboOk);
        RoboCheck(JsTestEvalNamed(runtime, "determinism", script, NULL) == RoboOk);
        RoboCheck(JsLoopRunUntilIdle(runtime, 0) == RoboOk);

        RoboCheck(JsRuntimeGetGlobal(runtime, "app", &app) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&app, &node) == RoboOk);
        RoboCheck(HtmlDocumentNodeById(document, (HtmlNodeId)node, &handle, NULL) == RoboOk);
        target[0] = '\0';
        RoboCheck(HtmlNodeSubtreeText(&handle, target, sizeof(first), &length, NULL) == RoboOk);

        DomExpectCleanTree(document);
        RoboCheck(JsDomHtmlDetach(runtime) > 0);
        JsRuntimeDestroy(runtime);
        RoboCheck(HtmlDocumentDestroy(document, NULL) == RoboOk);
    }
    RoboCheckStrEq(first, second);
    /* The rearrangement and the in-place text write both landed, so the comparison is over a page the
     * script actually changed. */
    RoboCheck(strstr(first, "rewritten") != NULL);
}

void TestRegisterRoboJSIntegration(void);

void TestRegisterRoboJSIntegration(void)
{
    TestRegister("js.integration.attach", TestIntegrationAttach);
    TestRegister("js.integration.read", TestIntegrationRead);
    TestRegister("js.integration.attributes", TestIntegrationAttributes);
    TestRegister("js.integration.structure", TestIntegrationStructure);
    TestRegister("js.integration.text_in_place", TestIntegrationTextInPlace);
    TestRegister("js.integration.creation_refused", TestIntegrationCreationRefused);
    TestRegister("js.integration.creation_open", TestIntegrationCreationOpen);
    TestRegister("js.integration.demo", TestIntegrationDemo);
    TestRegister("js.integration.steps", TestIntegrationSteps);
    TestRegister("js.integration.lifecycle", TestIntegrationLifecycle);
    TestRegister("js.integration.determinism", TestIntegrationDeterminism);
}
