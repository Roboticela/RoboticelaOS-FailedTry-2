/*
 * The RoboJS host suite (Phase 16.10–16.14) — `js.dom.*`, `js.event.*`, `js.timer.*`, `js.loop.*`.
 *
 * Everything here runs over `RoboJSTestSupport.h`'s fixture DOM rather than over RoboHTML, which is
 * the point: the DOM binding's own behaviour — wrapper interning, the selector grammar, listener
 * order, `preventDefault`, the pending/unmaterialized case — is asserted without a parser, a cascade
 * or a layout in the picture. `SuiteRoboJSIntegration.c` is where the *real* adapter is exercised.
 *
 * Three cases are worth calling out because they are the phase's own acceptance criteria rather than
 * a module door:
 *
 *   `js.loop.microtasks`  §16.14's ordering requirement, synchronously, on a deterministic clock: a
 *                         microtask queued before a `setTimeout(f, 0)` runs first, the drain runs a
 *                         microtask queued *by* a microtask, and a self-requeuing chain is cut at
 *                         `maxMicrotasksPerCycle` with the runtime still usable.
 *   `js.timer.schedule`   §16.12's rule that a timer callback is never called from `setTimeout`
 *                         itself — the script's own later statements run first — and that a repeating
 *                         timer's id survives its firings.
 *   `js.event.deferred`   the threading rule: a posted event does not run a listener until a loop
 *                         boundary, which is what makes "a device thread posts" true.
 */

#include "RoboJSTestSupport.h"

/* ------------------------------------------------------------------ */
/* 16.10 — the DOM binding                                             */
/* ------------------------------------------------------------------ */

static void TestDomInstall(void)
{
    JsTestDom dom;
    JsDomHost host;
    JsRuntimeConfig config;
    JsRuntime *runtime;
    char text[64];

    JsTestDomBuild(&dom);
    JsTestDomHostInit(&host, &dom);
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);

    RoboCheck(JsDomIsInstalled(runtime) == 0);
    RoboCheck(JsDomDocumentObject(runtime) == NULL);
    /* A runtime with no DOM is a supported configuration, not a broken one. */
    RoboCheck(JsRuntimeEvaluate(runtime, "no-dom", "var v = typeof document;", -1, NULL) == RoboOk);
    {
        char kind[32];

        JsTestGlobalString(runtime, "var shell = typeof document;", "shell", kind, sizeof(kind));
        RoboCheckStrEq(kind, "undefined");
    }

    RoboCheck(JsDomInstall(NULL, &host) == RoboInvalidArgument);
    RoboCheck(JsDomInstall(runtime, NULL) == RoboInvalidArgument);
    RoboCheck(JsDomInstall(runtime, &host) == RoboOk);
    RoboCheck(JsDomIsInstalled(runtime) == 1);
    RoboCheck(JsDomDocumentObject(runtime) != NULL);
    RoboCheck(JsDomNodeKindOf(runtime, 1) == (uint32_t)RoboJsDomNodeDocument);
    RoboCheck(JsDomNodeIsLive(runtime, 1) == 1);
    RoboCheck(JsDomNodeIsLive(runtime, 9999) == 0);

    /* `document` and `window` are the two globals 16.10's acceptance names, and `window` **is** the
     * global object — `window.foo = 1` and `foo` are one thing rather than two that can disagree. */
    JsTestGlobalString(runtime, "var d = typeof document;", "d", text, sizeof(text));
    RoboCheckStrEq(text, "object");
    JsTestGlobalString(runtime, "var w = typeof window;", "w", text, sizeof(text));
    RoboCheckStrEq(text, "object");
    RoboCheck(JsRuntimeEvaluate(runtime, "win", "window.probe = 41;", -1, NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime, "var probeRead = probe;", "probeRead") == 41.0);

    /* Installing the same host twice is idempotent; the current document answers its own root. */
    RoboCheck(JsDomInstall(runtime, &host) == RoboOk);
    RoboCheckEqInt(JsDomWrapperCount(runtime) > 0, 1);

    RoboCheck(JsDomUninstall(runtime) > 0);
    RoboCheck(JsDomIsInstalled(runtime) == 0);
    RoboCheckEqInt(JsDomWrapperCount(runtime), 0);

    JsRuntimeDestroy(runtime);
}

static void TestDomQueries(void)
{
    JsTestDom dom;
    JsDomHost host;
    JsRuntimeConfig config;
    JsRuntime *runtime;

    JsTestDomBuild(&dom);
    JsTestDomHostInit(&host, &dom);
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomInstall(runtime, &host) == RoboOk);

    /* `getElementById`, and the wrapper is interned: the same node is the same object. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var app = document.getElementById(\"app\");\n"
                                "var again = document.getElementById(\"app\");\n"
                                "var same = (app === again) ? 1 : 0;",
                                "same") == 1.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var missing = document.getElementById(\"nope\") === null ? 1 : 0;",
                                "missing") == 1.0);

    /* Element identity properties, read from the host. */
    {
        char text[64];

        JsTestGlobalString(runtime, "var tag = app.tagName;", "tag", text, sizeof(text));
        RoboCheckStrEq(text, "DIV");
        JsTestGlobalString(runtime, "var name = app.nodeName;", "name", text, sizeof(text));
        RoboCheckStrEq(text, "div");
        JsTestGlobalString(runtime, "var id = app.id;", "id", text, sizeof(text));
        RoboCheckStrEq(text, "app");
    }
    RoboCheck(JsTestGlobalNumber(runtime, "var nodeType = app.nodeType;", "nodeType") == 1.0);

    /* Selectors: a comma list of tag/#id/.class compounds, matched over the host's tree. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var intro = document.querySelector(\".intro\");\n"
                                "var ok = (intro !== null && intro.nodeName === \"p\") ? 1 : 0;",
                                "ok") == 1.0);
    {
        char text[32];

        JsTestGlobalString(runtime,
                           "var first = document.querySelector(\"li\");\n"
                           "var firstText = first.textContent;",
                           "firstText",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "one");
    }
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var items = document.querySelectorAll(\"li\");\n"
                                "var listLength = items.length;",
                                "listLength") == 2.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var both = document.querySelectorAll(\"li, .intro\");\n"
                                "var bothLength = both.length;",
                                "bothLength") == 3.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var byTag = document.getElementsByTagName(\"li\");\n"
                                "var tagLength = byTag.length;",
                                "tagLength") == 2.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var scoped = app.querySelector(\"li\");\n"
                                "var scopedIsNull = scoped === null ? 1 : 0;",
                                "scopedIsNull") == 1.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var matched = document.querySelector(\"p\").matches(\".lead\") ? 1 : 0;",
                                "matched") == 1.0);

    /* The selector grammar is validated before the walk, and an unsupported one is a located
     * SyntaxError rather than a silent no-match. */
    {
        Result status = JsRuntimeEvaluate(runtime,
                                          "bad-selector",
                                          "var x = document.querySelector(\"div > p\");",
                                          -1,
                                          NULL);
        JsValue thrown;

        RoboCheck(status != RoboOk);
        thrown = JsValueUndefined();
        RoboCheck(JsRuntimePeekException(runtime, &thrown) == RoboOk);
        RoboCheckEqInt(JsExceptionKindOf(&thrown), RoboJsErrorSyntax);
        JsRuntimeClearException(runtime);
    }
    {
        /* And the validator is a door of its own, so a host can refuse a selector before a walk. */
        char reason[128];

        RoboCheck(JsDomSelectorValidate("div", reason, (int32_t)sizeof(reason)) == 1);
        RoboCheck(JsDomSelectorMatches(runtime, (JsDomNodeId)1, "html", reason, (int32_t)sizeof(reason)) ==
                  0); /* the document node is not an element */
    }
    {
        char reason[128];

        RoboCheck(JsDomSelectorValidate("div p", reason, (int32_t)sizeof(reason)) == 0);
        RoboCheck(reason[0] != '\0');
        RoboCheck(JsDomSelectorValidate("div", reason, (int32_t)sizeof(reason)) == 1);
        RoboCheck(JsDomSelectorValidate("#a, .b, c, *", reason, (int32_t)sizeof(reason)) == 1);
        RoboCheck(JsDomSelectorValidate("[href]", reason, (int32_t)sizeof(reason)) == 0);
        RoboCheck(JsDomSelectorValidate("", reason, (int32_t)sizeof(reason)) == 0);
    }

    /* Tree reads: parent/sibling links, `children`, `childNodes` and `attributes`. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var p = document.querySelector(\"p\");\n"
                                "var parentIsBody = (p.parentNode === document.body) ? 1 : 0;",
                                "parentIsBody") == 1.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var lis = document.querySelectorAll(\"li\");\n"
                                "var siblings = (lis[0].nextSibling === lis[1]) ? 1 : 0;",
                                "siblings") == 1.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var ul = document.querySelector(\"ul\");\n"
                                "var elementChildren = ul.children.length;",
                                "elementChildren") == 2.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var li = document.querySelector(\"li\");\n"
                                "var textChildren = li.childNodes.length;",
                                "textChildren") == 1.0);
    /* `className` is a *string*: the fixture's `p` carries `class="intro lead"`, so the claim is that
     * it reads back and is not empty, not that it converts to a number (it does not). */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var p2 = document.querySelector(\"p\");\n"
                                "var hasClass = p2.className.length > 0 ? 1 : 0;",
                                "hasClass") == 1.0);
    {
        char text[64];

        JsTestGlobalString(runtime, "var classes = document.querySelector(\"p\").className;",
                           "classes", text, sizeof(text));
        RoboCheckStrEq(text, "intro lead");
    }
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var attrs = document.getElementById(\"app\").attributes.length;",
                                "attrs") == 1.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var hasIt = document.getElementById(\"app\").hasAttribute(\"id\") ? 1 : 0;",
                                "hasIt") == 1.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var lacks = document.getElementById(\"app\").hasAttribute(\"data-x\") ? 1 : 0;",
                                "lacks") == 0.0);

    JsRuntimeDestroy(runtime);
}

static void TestDomMutation(void)
{
    JsTestDom dom;
    JsDomHost host;
    JsRuntimeConfig config;
    JsRuntime *runtime;
    int32_t invalidationsBefore;

    JsTestDomBuild(&dom);
    JsTestDomHostInit(&host, &dom);
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomInstall(runtime, &host) == RoboOk);

    invalidationsBefore = dom.invalidations;

    /* The phase's §16.10 acceptance, over the fixture: `textContent` is a write to the real node. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "text",
                                "document.getElementById(\"app\").textContent = \"Hello\";",
                                -1,
                                NULL) == RoboOk);
    {
        JsDomNodeId app = JsTestDomFindById(&dom, "app");

        RoboCheck(app != 0);
        {
            char text[64];

            RoboCheck(JsTestDomTextContent(&dom, app, text, (int32_t)sizeof(text)) > 0);
            RoboCheckStrEq(text, "Hello");
        }
    }
    RoboCheck(dom.invalidations > invalidationsBefore);

    /* createElement + appendChild materializes through the host, and the tree is the host's. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "build",
                                "var made = document.createElement(\"span\");\n"
                                "made.textContent = \"fresh\";\n"
                                "document.getElementById(\"app\").appendChild(made);",
                                -1,
                                NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var appended = document.querySelector(\"span\").textContent === \"fresh\" ? 1 : 0;",
                                "appended") == 1.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var parentOk = (document.querySelector(\"span\").parentNode === "
                                "document.getElementById(\"app\")) ? 1 : 0;",
                                "parentOk") == 1.0);

    /* setAttribute / getAttribute / removeAttribute, and `style` as a write door onto one attribute. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "attrs",
                                "var node = document.getElementById(\"app\");\n"
                                "node.setAttribute(\"data-count\", \"3\");\n"
                                "node.style.setProperty(\"color\", \"red\");",
                                -1,
                                NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var count = document.getElementById(\"app\").getAttribute(\"data-count\") === \"3\" ? 1 : 0;",
                                "count") == 1.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var styleRead = document.getElementById(\"app\").getAttribute(\"style\") === "
                                "\"color:red\" ? 1 : 0;",
                                "styleRead") == 1.0);
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "remove",
                                "document.getElementById(\"app\").removeAttribute(\"data-count\");",
                                -1,
                                NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var gone = document.getElementById(\"app\").hasAttribute(\"data-count\") ? 1 : 0;",
                                "gone") == 0.0);

    /* removeChild detaches; `contains` answers ancestry; and a script's mutation is counted by the
     * host, which is what "the real DOM" means as a number rather than a claim. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "remove-child",
                                "var app2 = document.getElementById(\"app\");\n"
                                "var span = document.querySelector(\"span\");\n"
                                "app2.removeChild(span);\n"
                                "var stillThere = app2.contains(span) ? 1 : 0;",
                                -1,
                                NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime, "var stillThere2 = stillThere;", "stillThere2") == 0.0);
    RoboCheck(dom.mutations > 0);

    /* A NULL argument is a refusal rather than a crash: `appendChild(1)` is a TypeError. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "bad-append",
                                "document.getElementById(\"app\").appendChild(1);",
                                -1,
                                NULL) != RoboOk);
    JsRuntimeClearException(runtime);

    JsRuntimeDestroy(runtime);
}

static void TestDomLifecycle(void)
{
    JsTestDom dom;
    JsDomHost host;
    JsRuntimeConfig config;
    JsRuntime *runtime;
    JsDomHtmlCounters before;
    JsDomHtmlCounters after;

    JsTestDomBuild(&dom);
    JsTestDomHostInit(&host, &dom);
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomInstall(runtime, &host) == RoboOk);

    /* The document dies underneath the runtime: reads still answer, mutations refuse and are counted
     * rather than touching freed state. That is the 16.16 lifecycle claim. */
    dom.alive = 0;
    RoboCheck(JsDomNodeIsLive(runtime, 1) == 0);
    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));
    RoboCheck(JsDomHtmlGetCounters(runtime, &before) == RoboOk);
    {
        Result status = JsRuntimeEvaluate(runtime,
                                          "dead-document",
                                          "document.getElementById(\"app\").textContent = \"no\";",
                                          -1,
                                          NULL);

        RoboCheck(status != RoboOk);
        JsRuntimeClearException(runtime);
    }
    RoboCheck(JsDomHtmlGetCounters(runtime, &after) == RoboOk);
    RoboCheck(after.refusals > before.refusals);

    /* The host comes back to life, and the same script that was refused now succeeds. That pairing is
     * what makes the refusal above a fact about the document rather than about the script. */
    dom.alive = 1;
    RoboCheck(JsDomNodeIsLive(runtime, 1) == 1);
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "alive-again",
                                "document.getElementById(\"app\").setAttribute(\"ok\", \"1\");",
                                -1,
                                NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime,
                                 "var ok = document.getElementById(\"app\").hasAttribute(\"ok\") ? 1 : 0;",
                                 "ok") == 1.0);

    /* Uninstalling drops every wrapper and leaves the runtime a valid "no DOM" one. */
    RoboCheck(JsDomUninstall(runtime) > 0);
    RoboCheckEqInt(JsDomWrapperCount(runtime), 0);
    RoboCheck(JsRuntimeEvaluate(runtime, "gone", "var d = typeof document;", -1, NULL) == RoboOk);
    {
        char text[32];

        JsTestGlobalString(runtime, "var d2 = typeof document;", "d2", text, sizeof(text));
        RoboCheckStrEq(text, "undefined");
    }

    JsRuntimeDestroy(runtime);
}

/* ------------------------------------------------------------------ */
/* 16.11 — events                                                      */
/* ------------------------------------------------------------------ */

static void TestEventBinding(void)
{
    JsTestDom dom;
    JsDomHost host;
    JsRuntimeConfig config;
    JsRuntime *runtime;

    JsTestDomBuild(&dom);
    JsTestDomHostInit(&host, &dom);
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomInstall(runtime, &host) == RoboOk);

    /* The phase's §18 listener, synchronously: `addEventListener` registers, `dispatchEvent` runs. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "listener",
                                "var order = \"\";\n"
                                "var button = document.getElementById(\"app\");\n"
                                "button.addEventListener(\"click\", function () { order = order + \"a\"; });\n"
                                "button.addEventListener(\"click\", function () { order = order + \"b\"; });\n"
                                "var event = { type: \"click\" };\n"
                                "button.dispatchEvent(event);",
                                -1,
                                NULL) == RoboOk);
    {
        char text[64];

        JsTestGlobalString(runtime, "var seen = order;", "seen", text, sizeof(text));
        RoboCheckStrEq(text, "ab"); /* listener order is insertion order */
    }

    /* `removeEventListener` takes the first listener with that exact function value. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "remove-listener",
                                "var reset = \"\";\n"
                                "function first() { reset = reset + \"1\"; }\n"
                                "function second() { reset = reset + \"2\"; }\n"
                                "var node = document.getElementById(\"app\");\n"
                                "node.addEventListener(\"go\", first);\n"
                                "node.addEventListener(\"go\", second);\n"
                                "node.removeEventListener(\"go\", first);\n"
                                "node.dispatchEvent({ type: \"go\" });",
                                -1,
                                NULL) == RoboOk);
    {
        char text[64];

        JsTestGlobalString(runtime, "var afterRemove = reset;", "afterRemove", text, sizeof(text));
        RoboCheckStrEq(text, "2");
    }

    /* A listener on an ancestor sees an event dispatched at a descendant, and `event.target` still
     * names the descendant while `event.currentTarget` names the node the listener sits on. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "propagation",
                                "var trail = \"\";\n"
                                "var body = document.body;\n"
                                "var li = document.querySelector(\"li\");\n"
                                "var targetName = \"\";\n"
                                "var currentName = \"\";\n"
                                "body.addEventListener(\"bubble\", function (e) {\n"
                                "  trail = trail + \"body\";\n"
                                "  targetName = e.target.nodeName;\n"
                                "  currentName = e.currentTarget.nodeName;\n"
                                "});\n"
                                "li.addEventListener(\"bubble\", function () { trail = trail + \"li\"; });\n"
                                "li.dispatchEvent({ type: \"bubble\" });",
                                -1,
                                NULL) == RoboOk);
    {
        char text[64];

        JsTestGlobalString(runtime, "var trail2 = trail;", "trail2", text, sizeof(text));
        RoboCheckStrEq(text, "libody");
        JsTestGlobalString(runtime, "var t3 = targetName;", "t3", text, sizeof(text));
        RoboCheckStrEq(text, "li");
        JsTestGlobalString(runtime, "var t4 = currentName;", "t4", text, sizeof(text));
        RoboCheckStrEq(text, "body");
    }

    /* `preventDefault` is reported back to the caller, which is what a scroll host consults. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "prevent-default",
                                "var wheel = document.getElementById(\"app\");\n"
                                "wheel.addEventListener(\"wheel\", function (e) { e.preventDefault(); });\n"
                                "var accepted = wheel.dispatchEvent({ type: \"wheel\" });",
                                -1,
                                NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime, "var accepted2 = accepted ? 1 : 0;", "accepted2") == 1.0);

    /* A listener table is a table: the count is a door, and removal/dropping are counted. */
    RoboCheck(JsEventListenerCount(runtime, JsTestDomFindById(&dom, "app")) >= 1);
    RoboCheck(JsEventTotalListenerCount(runtime) > 0);

    /* Dispatching an event with no listeners is not an error. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "no-listeners",
                                "var quiet = document.querySelector(\"ul\");\n"
                                "var handled = quiet.dispatchEvent({ type: \"nobody\" });",
                                -1,
                                NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime, "var handled2 = handled ? 1 : 0;", "handled2") == 0.0);

    RoboCheck(JsEventDispatchCount(runtime) >= 4);

    /* A listener that throws is reported and the runtime survives. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "throwing-listener",
                                "var boomer = document.querySelector(\"ul\");\n"
                                "boomer.addEventListener(\"boom\", function () { throw new Error(\"listener\"); });",
                                -1,
                                NULL) == RoboOk);
    {
        Result status = JsRuntimeEvaluate(runtime,
                                          "boom",
                                          "document.querySelector(\"ul\").dispatchEvent({ type: \"boom\" });",
                                          -1,
                                          NULL);

        RoboCheck(status != RoboOk);
        JsRuntimeClearException(runtime);
    }
    RoboCheck(JsEventFailureCount(runtime) >= 1);
    RoboCheck(JsRuntimeEvaluate(runtime, "after", "var fine = 1;", -1, NULL) == RoboOk);

    RoboCheck(JsEventDropAll(runtime) >= 1);
    RoboCheckEqInt(JsEventTotalListenerCount(runtime), 0);

    JsRuntimeDestroy(runtime);
}

static void TestEventDeferred(void)
{
    JsTestDom dom;
    JsDomHost host;
    JsRuntimeConfig config;
    JsRuntime *runtime;
    int32_t ran = 0;

    JsTestDomBuild(&dom);
    JsTestDomHostInit(&host, &dom);
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomInstall(runtime, &host) == RoboOk);

    RoboCheck(JsTestEval(runtime,
                         "var trail = \"\";\n"
                         "var app = document.getElementById(\"app\");\n"
                         "app.addEventListener(\"click\", function () { trail = trail + \"ran\"; });",
                         NULL) == RoboOk);

    /* A posted event does **not** run a listener: that is what makes "a device thread posts and does
     * not call" a property of the design rather than a promise. */
    {
        JsEventRecord record;
        JsValue appValue;
        JsDomNodeId appNode = 0;

        /* The record's target is the node the listener sits on -- a dispatch builds its path from the
         * target upward, so a record aimed at the document would never reach the `#app` listener. */
        RoboCheck(JsRuntimeGetGlobal(runtime, "app", &appValue) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&appValue, &appNode) == RoboOk);
        RoboCheck(JsEventRecordInit(NULL, "click", 0) == RoboInvalidArgument);
        RoboCheck(JsEventRecordInit(&record, "click", appNode) == RoboOk);
        RoboCheck(JsEventQueue(runtime, &record) == RoboOk);
        record.clientX = 12.0;
        record.clientY = 34.0;
        record.modifiers = JsEventModifierShift;
        snprintf(record.key, sizeof(record.key), "%s", "a");
        RoboCheck(JsEventQueue(runtime, &record) == RoboOk);
    }
    RoboCheckEqInt(JsLoopPendingTaskCount(runtime), 2);
    {
        char text[64];

        JsTestGlobalString(runtime, "var before = trail;", "before", text, sizeof(text));
        RoboCheckStrEq(text, ""); /* nothing ran yet */
    }

    /* One boundary runs exactly one event. */
    RoboCheck(JsLoopRunOne(runtime, &ran) == RoboOk);
    RoboCheckEqInt(ran, 1);
    RoboCheckEqInt(JsLoopPendingTaskCount(runtime), 1);
    RoboCheck(JsLoopRunUntilIdle(runtime, 0) == RoboOk);
    RoboCheckEqInt(JsLoopPendingTaskCount(runtime), 0);
    {
        char text[64];

        JsTestGlobalString(runtime, "var after = trail;", "after", text, sizeof(text));
        RoboCheckStrEq(text, "ranran");
    }

    JsRuntimeDestroy(runtime);
}

/* ------------------------------------------------------------------ */
/* 16.12 — timers                                                      */
/* ------------------------------------------------------------------ */

static void TestTimers(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    int64_t id = 0;
    JsTimerInfo info;

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);

    /* §16.12's rule: scheduling does not call. The script's later statements run first, which is
     * exactly what the phase's §21 demo is about. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "schedule",
                                "var result = [];\n"
                                "result.push(\"start\");\n"
                                "setTimeout(function () { result.push(\"timer\"); }, 0);\n"
                                "result.push(\"end\");\n"
                                "var beforeLoop = result.length;",
                                -1,
                                NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime, "var len = beforeLoop;", "len") == 2.0);
    {
        char text[64];

        JsTestGlobalString(runtime,
                           "var joined = \"\";\n"
                           "for (var i = 0; i < result.length; i = i + 1) { joined = joined + result[i] + \",\"; }",
                           "joined",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "start,end,");
    }
    RoboCheckEqInt(JsTimerCount(runtime), 1);
    RoboCheckEqInt(JsTimerDueCount(runtime, JsRuntimeNowMs(runtime)), 1);

    /* The loop fires it, and the order is the script's then the timer's. */
    RoboCheck(JsLoopPump(runtime, NULL) == RoboOk);
    {
        char text[128];

        JsTestGlobalString(runtime,
                           "var joined2 = \"\";\n"
                           "for (var i = 0; i < result.length; i = i + 1) { joined2 = joined2 + result[i] + \",\"; }",
                           "joined2",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "start,end,timer,");
    }
    RoboCheckEqInt(JsTimerCount(runtime), 0); /* a one-shot is gone once it fired */

    /* Delays are honoured on the clock, not on wall time: nothing fires before the due time. */
    RoboCheck(JsTimerSet(runtime,
                         1000,
                         &(JsValue){0},
                         NULL,
                         0,
                         0,
                         &id) == RoboInvalidArgument); /* not callable */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "delayed",
                                "var fired = 0;\n"
                                "setTimeout(function () { fired = fired + 1; }, 1000);",
                                -1,
                                NULL) == RoboOk);
    RoboCheckEqInt(JsTimerCount(runtime), 1);
    RoboCheckEqInt(JsTimerDueCount(runtime, JsRuntimeNowMs(runtime)), 0);
    RoboCheck(JsLoopPump(runtime, NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime, "var zero = fired;", "zero") == 0.0);
    RoboCheck(JsRuntimeAdvanceClock(runtime, 999) == RoboOk);
    RoboCheckEqInt(JsTimerDueCount(runtime, JsRuntimeNowMs(runtime)), 0);
    RoboCheck(JsLoopPump(runtime, NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime, "var stillZero = fired;", "stillZero") == 0.0);
    RoboCheck(JsRuntimeAdvanceClock(runtime, 1) == RoboOk);
    RoboCheckEqInt(JsTimerDueCount(runtime, JsRuntimeNowMs(runtime)), 1);
    RoboCheck(JsLoopPump(runtime, NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime, "var one = fired;", "one") == 1.0);

    /* `clearTimeout` cancels, and `clearInterval` stops a repeating timer mid-flight. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "clear",
                                "var ticks = 0;\n"
                                "var handle = setTimeout(function () { ticks = ticks + 1; }, 0);\n"
                                "clearTimeout(handle);",
                                -1,
                                NULL) == RoboOk);
    RoboCheckEqInt(JsTimerCount(runtime), 0);
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "interval",
                                "var beats = 0;\n"
                                "var repeat = setInterval(function () { beats = beats + 1; }, 100);",
                                -1,
                                NULL) == RoboOk);
    RoboCheckEqInt(JsTimerCount(runtime), 1);
    RoboCheck(JsRuntimeAdvanceClock(runtime, 100) == RoboOk);
    RoboCheck(JsLoopPump(runtime, NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime, "var beats1 = beats;", "beats1") == 1.0);
    RoboCheckEqInt(JsTimerCount(runtime), 1); /* the record survives its firing */
    RoboCheck(JsRuntimeAdvanceClock(runtime, 100) == RoboOk);
    RoboCheck(JsLoopPump(runtime, NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime, "var beats2 = beats;", "beats2") == 2.0);
    RoboCheck(JsRuntimeEvaluate(runtime, "stop", "clearInterval(repeat);", -1, NULL) == RoboOk);
    RoboCheckEqInt(JsTimerCount(runtime), 0);
    RoboCheck(JsRuntimeAdvanceClock(runtime, 500) == RoboOk);
    RoboCheck(JsLoopPump(runtime, NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime, "var beats3 = beats;", "beats3") == 2.0);

    /* Ids are runtime-local, monotonic, and a foreign id is not found rather than cleared. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "ids",
                                "var first = setTimeout(function () { }, 5);\n"
                                "var second = setTimeout(function () { }, 5);",
                                -1,
                                NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime, "var secondBigger = (second > first) ? 1 : 0;",
                                 "secondBigger") == 1.0);
    RoboCheck(JsTimerClear(runtime, 999999) == RoboNotFound);
    RoboCheck(JsTimerInfoOf(runtime, 999999, &info) == RoboNotFound);
    /* The id the script was handed, not a guessed one: ids are monotonic and never reused, so a
     * literal would be a claim about how many timers the earlier cases scheduled. */
    {
        JsValue firstId;

        RoboCheck(JsRuntimeGetGlobal(runtime, "first", &firstId) == RoboOk);
        RoboCheck(JsTimerInfoOf(runtime, (int64_t)JsValueToNumber(&firstId), &info) == RoboOk);
    }
    RoboCheckEqInt(info.active, 1);
    RoboCheckEqInt(info.repeating, 0);

    /* Dropping every timer is the window-destroyed path, and it reports what it took. */
    RoboCheck(JsRuntimeEvaluate(runtime, "more", "var t = setTimeout(function () { }, 50);", -1, NULL) ==
              RoboOk);
    RoboCheck(JsTimerCancelAll(runtime) >= 1);
    RoboCheckEqInt(JsTimerCount(runtime), 0);
    /* A cleared timer's callback never runs. */
    RoboCheck(JsLoopRunUntilIdle(runtime, 0) == RoboOk);
    {
        char text[64];

        JsTestGlobalString(runtime, "var quiet = (typeof ticks === \"number\") ? ticks : -1;",
                           "quiet", text, sizeof(text));
        RoboCheckStrEq(text, "0");
    }

    JsRuntimeDestroy(runtime);
}

/* ------------------------------------------------------------------ */
/* 16.13 / 16.14 — the loop and microtasks                             */
/* ------------------------------------------------------------------ */

static void TestMicrotasks(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    int ran = 0;

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);

    /* §22's demo: the microtask runs after the synchronous statements that queued it. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "microtask",
                                "var result = [];\n"
                                "result.push(\"start\");\n"
                                "queueMicrotask(function () { result.push(\"microtask\"); });\n"
                                "result.push(\"end\");",
                                -1,
                                NULL) == RoboOk);
    RoboCheckEqInt(JsLoopPendingMicrotaskCount(runtime), 1);
    RoboCheck(JsLoopRunOne(runtime, &ran) == RoboOk);
    RoboCheckEqInt(JsLoopPendingMicrotaskCount(runtime), 0);
    {
        char text[128];

        JsTestGlobalString(runtime,
                           "var joined = \"\";\n"
                           "for (var i = 0; i < result.length; i = i + 1) { joined = joined + result[i] + \",\"; }",
                           "joined",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "start,end,microtask,");
    }

    /* 16.14's required ordering: microtask before timer, from one task. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "ordering",
                                "var order = [];\n"
                                "queueMicrotask(function () { order.push(\"microtask\"); });\n"
                                "setTimeout(function () { order.push(\"timer\"); }, 0);\n"
                                "order.push(\"sync\");",
                                -1,
                                NULL) == RoboOk);
    RoboCheck(JsLoopRunOne(runtime, &ran) == RoboOk); /* drains the microtask queue */
    RoboCheck(JsLoopPump(runtime, NULL) == RoboOk);   /* collects and runs the timer */
    {
        char text[128];

        JsTestGlobalString(runtime,
                           "var joined = \"\";\n"
                           "for (var i = 0; i < order.length; i = i + 1) { joined = joined + order[i] + \",\"; }",
                           "joined",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "sync,microtask,timer,");
    }

    /* A microtask queued by a microtask runs in the same drain, in order. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "chain",
                                "var chain = [];\n"
                                "queueMicrotask(function () {\n"
                                "  chain.push(\"one\");\n"
                                "  queueMicrotask(function () { chain.push(\"two\"); });\n"
                                "});\n"
                                "queueMicrotask(function () { chain.push(\"direct\"); });",
                                -1,
                                NULL) == RoboOk);
    RoboCheck(JsLoopRunOne(runtime, &ran) == RoboOk);
    {
        char text[128];

        JsTestGlobalString(runtime,
                           "var joined = \"\";\n"
                           "for (var i = 0; i < chain.length; i = i + 1) { joined = joined + chain[i] + \",\"; }",
                           "joined",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "one,direct,two,");
    }

    /* Starvation is cut rather than run forever: a chain that re-queues itself reaches the cap, the
     * loop reports it, and the runtime is still usable. */
    {
        JsRuntimeConfig bounded;
        JsRuntime *spinner;

        JsRuntimeConfigInit(&bounded);
        bounded.deterministic = 1;
        bounded.maxMicrotasksPerCycle = 8;
        spinner = JsRuntimeCreate(&bounded);
        RoboCheck(spinner != NULL);
        RoboCheck(JsRuntimeEvaluate(spinner,
                                    "starve",
                                    "var spins = 0;\n"
                                    "function again() {\n"
                                    "  spins = spins + 1;\n"
                                    "  if (spins < 1000) { queueMicrotask(again); }\n"
                                    "}\n"
                                    "queueMicrotask(again);",
                                    -1,
                                    NULL) == RoboOk);
        RoboCheck(JsLoopRunOne(spinner, &ran) != RoboNotSupported);
        RoboCheck(JsLoopMicrotaskOverflows(spinner) >= 1);
        RoboCheck(JsRuntimeEvaluate(spinner, "after", "var fine = 1;", -1, NULL) == RoboOk);
        JsRuntimeDestroy(spinner);
    }

    /* A microtask that throws is reported and the loop stays usable. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "throwing-microtask",
                                "queueMicrotask(function () { throw new Error(\"micro\"); });",
                                -1,
                                NULL) == RoboOk);
    {
        Result status = JsLoopRunOne(runtime, &ran);

        RoboCheck(status != RoboOk);
        JsRuntimeClearException(runtime);
    }
    RoboCheck(JsRuntimeEvaluate(runtime, "after", "var fine2 = 1;", -1, NULL) == RoboOk);

    JsRuntimeDestroy(runtime);
}

static void TestEventLoop(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    int ran = 0;

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);

    /* An idle loop answers "nothing ran" rather than failing. */
    RoboCheck(JsLoopRunOne(runtime, &ran) == RoboOk);
    RoboCheckEqInt(ran, 0);
    RoboCheck(JsLoopHasPendingWork(runtime) == 0);
    RoboCheck(JsLoopRunUntilIdle(runtime, 0) == RoboOk);

    /* Tasks run in FIFO order, and a stop request is honoured until it is cleared. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "queue",
                                "var log = [];\n"
                                "function push(name) { return function () { log.push(name); }; }\n"
                                "var f1 = push(\"one\");\n"
                                "var f2 = push(\"two\");\n"
                                "var f3 = push(\"three\");",
                                -1,
                                NULL) == RoboOk);
    {
        JsValue first = JsValueUndefined();
        JsValue second = JsValueUndefined();
        JsValue third = JsValueUndefined();

        RoboCheck(JsRuntimeGetGlobal(runtime, "f1", &first) == RoboOk);
        RoboCheck(JsRuntimeGetGlobal(runtime, "f2", &second) == RoboOk);
        RoboCheck(JsRuntimeGetGlobal(runtime, "f3", &third) == RoboOk);
        RoboCheck(JsLoopPostTask(runtime, RoboJsTaskNative, &first, NULL, NULL, 0) == RoboOk);
        RoboCheck(JsLoopPostTask(runtime, RoboJsTaskNative, &second, NULL, NULL, 0) == RoboOk);
        RoboCheck(JsLoopPostTask(runtime, RoboJsTaskNative, &third, NULL, NULL, 0) == RoboOk);
        RoboCheckEqInt(JsLoopPendingTaskCount(runtime), 3);

        JsLoopRequestStop(runtime, 1);
        RoboCheck(JsLoopRunOne(runtime, &ran) == RoboTimeout);
        RoboCheckEqInt(JsLoopPendingTaskCount(runtime), 3);
        JsLoopRequestStop(runtime, 0);

        RoboCheck(JsLoopRunUntilIdle(runtime, 0) == RoboOk);
        RoboCheckEqInt(JsLoopPendingTaskCount(runtime), 0);
        RoboCheckEqInt(JsLoopTasksRun(runtime), 3);
    }
    {
        char text[64];

        JsTestGlobalString(runtime,
                           "var joined = \"\";\n"
                           "for (var i = 0; i < log.length; i = i + 1) { joined = joined + log[i] + \",\"; }",
                           "joined",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "one,two,three,");
    }

    /* The cap is a refusal, not a silent give-up, and clearing drops everything. */
    RoboCheck(JsRuntimeEvaluate(runtime, "count", "var never = function () { };", -1, NULL) == RoboOk);
    {
        JsValue never = JsValueUndefined();

        RoboCheck(JsRuntimeGetGlobal(runtime, "never", &never) == RoboOk);
        RoboCheck(JsLoopPostTask(runtime, RoboJsTaskNative, &never, NULL, NULL, 0) == RoboOk);
    }
    RoboCheckEqInt(JsLoopClear(runtime), 1);
    RoboCheckEqInt(JsLoopPendingTaskCount(runtime), 0);

    /* A native task that throws is reported and the loop survives. */
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "thrower",
                                "var thrower = function () { throw new Error(\"task\"); };",
                                -1,
                                NULL) == RoboOk);
    {
        JsValue task = JsValueUndefined();

        RoboCheck(JsRuntimeGetGlobal(runtime, "thrower", &task) == RoboOk);
        RoboCheck(JsLoopPostTask(runtime, RoboJsTaskNative, &task, NULL, NULL, 0) == RoboOk);
        RoboCheck(JsLoopRunOne(runtime, &ran) != RoboOk);
        JsRuntimeClearException(runtime);
    }
    RoboCheck(JsLoopRunUntilIdle(runtime, 0) == RoboOk);

    /* `JsRuntimeRunUntilIdle` is the same door with the loop's own name, and a runtime without a loop
     * refuses it rather than pretending. */
    RoboCheck(JsRuntimeRunUntilIdle(runtime) == RoboOk);
    {
        JsRuntimeConfig bare;
        JsRuntime *noLoop;

        JsValue task;

        JsRuntimeConfigInit(&bare);
        bare.deterministic = 1;
        bare.enableEventLoop = 0;
        noLoop = JsRuntimeCreate(&bare);
        RoboCheck(noLoop != NULL);
        task = JsValueUndefined();
        RoboCheck(JsRuntimeRunUntilIdle(noLoop) == RoboNotSupported);
        /* A null *callback* is a caller error (`RoboInvalidArgument`) and is checked before the loop,
         * so the "no loop" answer needs something postable. */
        RoboCheck(JsLoopPostTask(noLoop, RoboJsTaskNative, &task, NULL, NULL, 0) == RoboNotSupported);
        JsRuntimeDestroy(noLoop);
    }

    JsRuntimeDestroy(runtime);
}

/* ------------------------------------------------------------------ */
/* Lifecycle: a destroyed runtime takes its work with it               */
/* ------------------------------------------------------------------ */

static void TestLifecycleCleanup(void)
{
    JsTestDom dom;
    JsDomHost host;
    JsRuntimeConfig config;
    JsRuntime *runtime;

    JsTestDomBuild(&dom);
    JsTestDomHostInit(&host, &dom);
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomInstall(runtime, &host) == RoboOk);

    RoboCheck(JsRuntimeEvaluate(runtime,
                                "busy",
                                "var handler = function () { };\n"
                                "document.getElementById(\"app\").addEventListener(\"click\", handler);\n"
                                "setTimeout(function () { }, 1000);\n"
                                "queueMicrotask(function () { });\n"
                                "var n = 0;\n"
                                "while (n < 100) { n = n + 1; }",
                                -1,
                                NULL) == RoboOk);
    RoboCheck(JsEventTotalListenerCount(runtime) >= 1);
    RoboCheck(JsTimerCount(runtime) == 1);
    RoboCheck(JsLoopPendingMicrotaskCount(runtime) == 1);
    RoboCheck(JsHeapLiveBytes(JsRuntimeHeap(runtime)) > 0);

    /* A reset reclaims everything and answers the same questions the same way: no listener, no timer,
     * no queued microtask, and a global that is gone. */
    RoboCheck(JsRuntimeReset(runtime) == RoboOk);
    RoboCheckEqInt(JsEventTotalListenerCount(runtime), 0);
    RoboCheckEqInt(JsTimerCount(runtime), 0);
    RoboCheckEqInt(JsLoopPendingMicrotaskCount(runtime), 0);
    RoboCheckEqInt(JsLoopPendingTaskCount(runtime), 0);
    {
        JsValue gone = JsValueUndefined();

        RoboCheck(JsRuntimeGetGlobal(runtime, "handler", &gone) == RoboNotFound);
    }
    /* The DOM survives a reset — the document is the caller's, not the heap's — and is reinstalled. */
    RoboCheck(JsDomIsInstalled(runtime) == 1);
    RoboCheck(JsRuntimeEvaluate(runtime,
                                "again",
                                "var total = 0;\n"
                                "for (var i = 0; i < 4; i = i + 1) { total = total + i; }",
                                -1,
                                NULL) == RoboOk);
    RoboCheck(JsTestGlobalNumber(runtime, "var total2 = total;", "total2") == 6.0);

    /* The counters the integration layer reads are readable at any point, and the door refuses a
     * NULL rather than filling one. */
    {
        JsDomHtmlCounters counters;

        RoboCheck(JsDomHtmlGetCounters(NULL, &counters) == RoboInvalidArgument);
        RoboCheck(JsDomHtmlGetCounters(runtime, &counters) == RoboOk);
        RoboCheck(counters.pendingCreated >= 0);
    }
    RoboCheck(JsDomUninstall(runtime) > 0);
    RoboCheck(JsDomIsInstalled(runtime) == 0);

    JsRuntimeDestroy(runtime);
}

/**
 * What a native binding saw when it tried to enter the loop from inside the loop.
 *
 * A file-scope static rather than a runtime global, because the observation has to survive the call
 * that made it and the assertion happens on the C side. One case uses it, so one slot is enough.
 */
#define JsLoopProbeMax 8
static Result jsLoopProbeResults[JsLoopProbeMax];
static int32_t jsLoopProbeCount;

static Result JsLoopProbeNative(JsRuntime *runtime,
                                JsObject *thisObject,
                                const JsValue *args,
                                int32_t argCount,
                                JsValue *out,
                                void *userdata)
{
    int ran = 0;
    Result status;
    int probe = jsLoopProbeCount;

    (void)thisObject;
    (void)args;
    (void)argCount;
    (void)userdata;
    jsLoopProbeCount += 1;
    status = JsLoopRunOne(runtime, &ran);
    if (probe < JsLoopProbeMax) {
        jsLoopProbeResults[probe] = status;
    }
    *out = JsValueUndefined();
    return RoboOk;
}

/**
 * The loop is not reentrant, and this is where that is proved rather than asserted.
 *
 * A `JsLoopRunOne` inside a listener or a microtask would run a task inside a task, and every ordering
 * statement in `EventLoop.md` and `Microtasks.md` assumes that cannot happen. The interpreter's own
 * `executing` guard cannot see it -- a task runs through the call door, not through `JsRuntimeEvaluate`
 * -- so the loop keeps its own flag and `JsLoopRunOne`/`JsLoopRunUntilIdle` refuse while it is set.
 *
 * The probe is a **native binding** because that is the shape a shell reaches for: a native callback
 * that wants "run the loop" from inside a listener. The failure mode of a missing guard is not a crash --
 * it is a task that quietly runs and an ordering claim that quietly stops being true -- so the case reads
 * the status the native observed instead of waiting for something to go wrong.
 */
static void TestLoopReentrancy(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    JsDomHost host;
    JsTestDom dom;
    JsObject *probe;
    int ran = 0;

    JsTestDomBuild(&dom);
    JsTestDomHostInit(&host, &dom);
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsDomInstall(runtime, &host) == RoboOk);

    jsLoopProbeCount = 0;
    probe = JsFunctionCreateNative(JsRuntimeHeap(runtime), "probeLoop", 0, JsLoopProbeNative, NULL,
                                   JsRuntimeFunctionPrototype(runtime));
    RoboCheck(probe != NULL);
    {
        JsValue callable = JsValueObject(probe);

        RoboCheck(JsRuntimeDefineGlobal(runtime, "probeLoop", &callable) == RoboOk);
    }

    /* **Inside an evaluation** it is already refused**, by the interpreter's own `executing` guard:
     * the runtime is mid-script, so there is no boundary to run. That is the first half of the rule
     * and the reason a caller cannot "drive the loop" from a callback that is itself running. */
    RoboCheck(JsTestEvalNamed(runtime, "probe-outside", "probeLoop();", NULL) == RoboOk);
    RoboCheckEqInt(jsLoopProbeCount, 1);
    RoboCheckEqInt((int64_t)jsLoopProbeResults[0], (int64_t)RoboInvalidArgument);

    /* From C, with no evaluation in progress, the same call runs a boundary. That pair is what makes
     * the refusals above a *state* rather than a permanently broken door. */
    RoboCheck(JsLoopRunOne(runtime, &ran) == RoboOk);
    RoboCheckEqInt(ran, 0);

    /* **From inside a microtask**, which is a boundary the drain owns; the loop must refuse. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "probe-microtask",
                              "queueMicrotask(probeLoop);",
                              NULL) == RoboOk);
    RoboCheck(JsLoopRunOne(runtime, &ran) == RoboOk);
    RoboCheckEqInt(jsLoopProbeCount, 2);
    RoboCheckEqInt((int64_t)jsLoopProbeResults[1], (int64_t)RoboInvalidArgument);

    /* And from inside a **listener**, which runs through the event path rather than the microtask
     * queue. A guard on one path and not the other is the shape this catches. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "probe-listener",
                              "var app = document.getElementById(\"app\");\n"
                              "app.addEventListener(\"click\", function () {\n"
                              "  probeLoop();\n"
                              "});",
                              NULL) == RoboOk);
    {
        JsValue app = JsValueUndefined();
        JsDomNodeId node = 0;
        JsEventRecord record;

        RoboCheck(JsRuntimeGetGlobal(runtime, "app", &app) == RoboOk);
        RoboCheck(JsDomNodeOfValue(&app, &node) == RoboOk);
        RoboCheck(JsEventRecordInit(&record, "click", node) == RoboOk);
        RoboCheck(JsEventQueue(runtime, &record) == RoboOk);
        RoboCheck(JsLoopRunOne(runtime, &ran) == RoboOk);
    }
    RoboCheckEqInt(jsLoopProbeCount, 3);
    RoboCheckEqInt((int64_t)jsLoopProbeResults[2], (int64_t)RoboInvalidArgument);

    /* A **native task** is the third path, and the one a host's own scheduler would take. */
    {
        JsValue callable = JsValueObject(probe);

        RoboCheck(JsLoopPostTask(runtime, RoboJsTaskNative, &callable, NULL, NULL, 0) == RoboOk);
        RoboCheck(JsLoopRunOne(runtime, &ran) == RoboOk);
    }
    RoboCheckEqInt(jsLoopProbeCount, 4);
    RoboCheckEqInt((int64_t)jsLoopProbeResults[3], (int64_t)RoboInvalidArgument);

    /* `JsLoopRunUntilIdle` is the same rule: a listener reaching for the *draining* door is refused
     * too, and nothing it was trying to reach ran. */
    {
        JsValue callable = JsValueObject(probe);

        RoboCheck(JsLoopPostTask(runtime, RoboJsTaskNative, &callable, NULL, NULL, 0) == RoboOk);
        RoboCheckEqInt(JsLoopPendingTaskCount(runtime), 1);
        RoboCheck(JsLoopRunUntilIdle(runtime, 0) == RoboOk);
        RoboCheckEqInt(JsLoopPendingTaskCount(runtime), 0);
    }
    RoboCheckEqInt(jsLoopProbeCount, 5);
    RoboCheckEqInt((int64_t)jsLoopProbeResults[4], (int64_t)RoboInvalidArgument);

    /* The flag does not stick: after every drain the loop is enterable again. */
    RoboCheck(JsRuntimeIsExecuting(runtime) == 0);
    RoboCheck(JsLoopRunOne(runtime, &ran) == RoboOk);
    RoboCheckEqInt(ran, 0);

    JsRuntimeDestroy(runtime);
}

void TestRegisterRoboJSHost(void);

void TestRegisterRoboJSHost(void)
{
    TestRegister("js.dom.install", TestDomInstall);
    TestRegister("js.dom.queries", TestDomQueries);
    TestRegister("js.dom.mutation", TestDomMutation);
    TestRegister("js.dom.lifecycle", TestDomLifecycle);
    TestRegister("js.event.binding", TestEventBinding);
    TestRegister("js.event.deferred", TestEventDeferred);
    TestRegister("js.timer.schedule", TestTimers);
    TestRegister("js.loop.microtasks", TestMicrotasks);
    TestRegister("js.loop.tasks", TestEventLoop);
    TestRegister("js.loop.reentrancy", TestLoopReentrancy);
    TestRegister("js.lifecycle.cleanup", TestLifecycleCleanup);
}
