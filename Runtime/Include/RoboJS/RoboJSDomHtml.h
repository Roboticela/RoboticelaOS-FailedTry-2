/*
 * The RoboHTML adapter (Phase 16.10) — the one place RoboJS and RoboHTML meet.
 *
 * `RoboJS/RoboJSDom.h` states the seam: RoboJS asks a `JsDomHost` what a node is and where it goes,
 * and never learns what a DOM *is*. This file implements that interface over `HtmlDocumentId`, so a
 * document that RoboHTML parsed and RoboLayout laid out is the same tree a script mutates, and a
 * mutation a script performs arrives at RoboCSS/RoboText through RoboHTML's own tree-mutated hook
 * rather than through a second, JavaScript-shaped invalidation path.
 *
 * ## One header in the module includes RoboHTML, and it is this one
 *
 * That is the same discipline `RoboPaintDispatch.c` follows toward RoboInput: the dependency is
 * named, small and replaceable rather than diffused. `RoboJS.h` deliberately does not include this
 * header, so a consumer that only evaluates scripts pays for none of it.
 *
 * ## Created-but-unattached elements, stated rather than faked
 *
 * RoboHTML's element model has no detached state: `HtmlElementCreate` links the element as a child of
 * a parent in the same call. `document.createElement("div")` therefore cannot hand back a node the
 * host already knows, and this adapter does not pretend otherwise — it holds the element's **tag and
 * attributes in a pending record** under a synthetic id (`>= 0x8000_0000_0000_0000`) and
 * materializes it through `HtmlElementCreate` at the first `appendChild`/`insertBefore`. Until then
 * the element answers its tag, its attributes and its (empty) child list, and `parentNode` is null;
 * after materialization every door reads the host.
 *
 * That is a *pending construction record*, not a second tree: it stores no children, no parent and no
 * text — only the three things the host's create door needs — and it ceases to exist the moment the
 * element joins the tree.
 *
 * ## `textContent` on an element writes real text nodes
 *
 * Setting `textContent` removes the element's children and appends one text node through
 * `HtmlTextCreate`, so the text a script wrote is the text RoboText shapes. Reading it concatenates
 * the subtree's text through `HtmlNodeSubtreeText`.
 *
 * ## Lifecycle
 *
 * `JsDomHtmlAttach` binds one document to one runtime and installs the DOM bindings;
 * `JsDomHtmlDetach` unbinds. A document destroyed while a runtime is attached leaves the runtime's
 * wrappers pointing at a dead document, and the adapter's `isLive` door then answers 0, which makes
 * every mutating door refuse rather than touch freed state — the case 16.16's lifecycle test asserts.
 */

#ifndef ROBO_JS_DOM_HTML_H
#define ROBO_JS_DOM_HTML_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboJS/RoboJSDom.h"
#include "RoboJS/RoboJSRuntime.h"
#include "RoboJS/RoboJSTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bind @p documentId to @p runtime: install the DOM host, the node prototypes and the `document`
 * global. The document must be live; the runtime must not already have a DOM installed (uninstall
 * first, because the wrappers and the document object belong to the document that went away).
 *
 * @return RoboOk; RoboInvalidArgument for a NULL runtime, an unknown document, or a runtime that
 *         already has a DOM; RoboOutOfMemory when the adapter's own record cannot be allocated.
 */
Result JsDomHtmlAttach(JsRuntime *runtime, HtmlDocumentId documentId);

/** Unbind the document from @p runtime, dropping the wrappers and the pending records. Answers the
 *  number of wrappers dropped; 0 when nothing was attached. */
int32_t JsDomHtmlDetach(JsRuntime *runtime);

/** The document @p runtime is attached to, or `RoboHtmlDocumentIdNone` when it is not. */
HtmlDocumentId JsDomHtmlDocument(JsRuntime *runtime);

/** Non-zero when the attached document is still live — the check a host makes before letting a
 *  script run again after a navigation. */
int JsDomHtmlIsLive(JsRuntime *runtime);

/**
 * Counters the integration tests assert on, each with one meaning.
 *
 * Reported rather than inferred, because "the DOM the script sees is the real one" should be a number,
 * not a claim -- and because several of the facts this adapter's boundary turns on are invisible in a
 * return code. "Did that `textContent` write create a node?" is `textNodesCreated` before and after;
 * "was that refusal the construction gate or a bad operand?" is `refusals` beside the exception's
 * message.
 *
 *  - `created`           elements a script asked for through `createElement`.
 *  - `pendingCreated`    of those, the ones still detached (a record, not a node).
 *  - `materialized`      a pending record that became a real RoboHTML node. Never decreases.
 *  - `textNodesCreated`  text nodes this adapter caused to exist, from `createTextNode` **and** from
 *                        the text a `textContent` write had to build. A write that reshaped nothing
 *                        leaves this alone, which is how `js.integration.text_in_place` proves it.
 *  - `appends`           successful `appendChild`/`insertBefore` calls, moves included.
 *  - `attributeWrites`   attribute writes that reached RoboHTML. Attribute writes are **not**
 *                        structural, so they do not move the document's tree version.
 *  - `textWrites`        successful text writes, whether in place or by construction.
 *  - `refusals`          a mutation the host would not perform: a dead document, a closed construction
 *                        gate, a full table. Includes refusals the caller sees as a thrown exception.
 */
typedef struct JsDomHtmlCounters {
    int32_t created;
    int32_t pendingCreated;
    int32_t materialized;
    int32_t textNodesCreated;
    int32_t appends;
    int32_t attributeWrites;
    int32_t textWrites;
    int32_t refusals;
} JsDomHtmlCounters;

Result JsDomHtmlGetCounters(JsRuntime *runtime, JsDomHtmlCounters *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_DOM_HTML_H */
