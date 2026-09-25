# RoboJS — DOM bindings (16.10)

How a script reaches a document, and — the part that took the most work — **what happens when the document's
model cannot represent what the script asked for.**

RoboJS has no DOM. `RoboJSDom.h` is a *host interface*: a vtable of callbacks a host implements over whatever tree
it has. Two implementations exist, and having two is the point:

| Implementation | What it is | What it proves |
|---|---|---|
| `Tests/RoboJS/RoboJSTestSupport.h`'s fixture | a 32-node tree in a struct, no parser, no layout | the binding's own behaviour — wrapper identity, selectors, listener order |
| `RoboJSDomHtml.c` | the shipped adapter over this tree's RoboHTML DOM | that the binding reaches a *real* document |

`RoboJSDomHtml.c` and `RoboJSDomHtml.h` are the **only** RoboJS files that include a RoboHTML header, and
`RoboJS.h` deliberately does not include the adapter — so a RoboJS consumer that never touches a document does not
link RoboHTML, and one that does includes both headers by name.

---

## 1. The seam

```c
typedef struct JsDomHost {
    void *userdata;
    int  (*isLive)(void *);                       /* 0 → every mutating door refuses */
    JsDomNodeId (*documentRoot)(void *);
    JsDomNodeId (*documentBody)(void *);
    JsDomNodeId (*findById)(void *, const char *);
    JsDomNodeId (*findByTag)(void *, JsDomNodeId root, const char *);
    JsDomNodeId (*createElement)(void *, const char *);
    JsDomNodeId (*createTextNode)(void *, const char *, int32_t);
    JsDomNodeId (*parent)(void *, JsDomNodeId);
    JsDomNodeId (*firstChild)(void *, JsDomNodeId);
    JsDomNodeId (*lastChild)(void *, JsDomNodeId);
    JsDomNodeId (*nextSibling)(void *, JsDomNodeId);
    JsDomNodeId (*previousSibling)(void *, JsDomNodeId);
    int32_t (*childCount)(void *, JsDomNodeId);
    JsDomNodeId (*childAt)(void *, JsDomNodeId, int32_t);
    uint32_t (*nodeKind)(void *, JsDomNodeId);    /* RoboJsDomNode* */
    int32_t (*nodeName)(void *, JsDomNodeId, char *, int32_t);
    int32_t (*textContent)(void *, JsDomNodeId, char *, int32_t);
    Result  (*setTextContent)(void *, JsDomNodeId, const char *, int32_t);
    int32_t (*attribute)(void *, JsDomNodeId, const char *, char *, int32_t);
    Result  (*setAttribute)(void *, JsDomNodeId, const char *, const char *);
    Result  (*removeAttribute)(void *, JsDomNodeId, const char *);
    int32_t (*attributeCount)(void *, JsDomNodeId);
    int32_t (*attributeNameAt)(void *, JsDomNodeId, int32_t, char *, int32_t);
    Result  (*appendChild)(void *, JsDomNodeId parent, JsDomNodeId child);
    Result  (*insertBefore)(void *, JsDomNodeId parent, JsDomNodeId child, JsDomNodeId before);
    Result  (*removeChild)(void *, JsDomNodeId parent, JsDomNodeId child);
    int     (*contains)(void *, JsDomNodeId ancestor, JsDomNodeId node);
    void    (*invalidate)(void *, JsDomNodeId);   /* optional: "this subtree's answer changed" */
} JsDomHost;
```

Three rules a host author must know:

1. **`isLive` gates everything mutating.** A host whose document went away answers 0 and every mutating door
   refuses; the *reads* still answer, because a shell between pages still runs scripts. That is the state
   `js.integration.lifecycle` pins.
2. **A node id is the host's.** RoboJS never interprets one, never allocates in the host's space, and never
   assumes the ids are small or dense. The adapter hands out RoboHTML node ids directly, which is why
   `HtmlDocumentNodeById` in a suite is a real proof.
3. **`invalidate` is optional and is not damage tracking.** It says "a consumer that cached something about this
   subtree should discard it". RoboHTML's own mutation hook already owns invalidation for the adapter, so the
   adapter passes `NULL` — the field exists for a host that needs it.

`JsDomInstall` requires a **complete** vtable (a missing callback is `RoboInvalidArgument`), because a half-installed
DOM is a script that fails on the one method its page happened to use.

---

## 2. What a script sees

### Wrappers, interned once

`JsDomWrapperForNode` interns one `JsObject *` per node. `document.getElementById("app")` twice answers the **same
object**, so `===` works for a listener comparing `event.target` to a saved reference, and so does a saved
reference after a mutation. `js.integration.attach` asserts it, and `JsDomRetargetWrapper` is the one place the
interning table is rewritten rather than appended to — see §4.

### Node properties, split in two halves

| Kind | Properties |
|---|---|
| **stored** — an ordinary own property, created on write | anything else: `el.custom = 1`, `el._state` |
| **computed** — answered by `JsHostOps` over the host | `id`, `className`, `value`, `nodeName`, `tagName`, `nodeType`, `parentNode`, `firstChild`, `lastChild`, `nextSibling`, `previousSibling`, `children`, `childNodes`, `firstElementChild`, `lastElementChild`, `attributes`, `style`, `ownerDocument`, `textContent`, `innerText`, `nodeValue`, `data`, and on the document `body`, `documentElement`, `title` |

The split is why `el.custom = 1` works (it becomes a real property) while `el.nodeName = "x"` does not silently
stick: the computed half is consulted first, and a name it does not recognise falls through to storage. A write to
a *computed* name that the host refuses is reported as the refusal, never as a stored property — a build that hid
it would make a failed mutation look like it worked.

`nodeName` keeps the source spelling (`"p"`) and `tagName` is uppercased for an HTML element (`"P"`), because the
DOM defines the two differently. `nodeType` is the number (1 element, 3 text, 9 document).

### Native bindings

| On a node | `getAttribute`, `setAttribute`, `removeAttribute`, `hasAttribute`, `appendChild`, `insertBefore`, `removeChild`, `remove`, `contains`, `matches`, `querySelector`, `querySelectorAll`, `getElementsByTagName`, `addEventListener`, `removeEventListener`, `dispatchEvent` |
| On the document | `getElementById`, `createElement`, `createTextNode` |
| On `style` | `setProperty`, `getPropertyValue`, `removeProperty` |

### The selector grammar

A comma-separated list of compounds, each a sequence of `tag`, `#id`, `.class` or `*`:

```text
"li"            "p.intro.lead"       "#list"        "*"        "ul, p"
```

**Not supported, and refused with a located reason rather than silently half-matched:** descendant and child
combinators (`#a b`, `#a > b`), attribute selectors (`[href]`), pseudo-classes (`:hover`, `:nth-child`), and the
sibling combinators. `JsDomSelectorValidate(selector, errBuf, cap)` answers **1 for valid, 0 for refused** and
fills `errBuf`; the native bindings raise a `SyntaxError` carrying the same message. `js.integration.attach`
asserts both directions, because a grammar that accepted a selector it could not match would return the wrong
nodes rather than no nodes.

`querySelectorAll` searches in **document order** and **excludes the context node itself**, which is what
`el.querySelectorAll` means and what `js.integration.creation_open` relies on.

---

## 3. The RoboHTML boundary — what the adapter can and cannot do

RoboHTML's tree builder finalizes a document when its input is exhausted. Finalization closes *node creation*
(`RoboHTMLMutation.h` states the boundary; `SuiteRoboHTMLMutation` and `SuiteRoboHTMLElement` **pin** it as a
decision), and `HtmlElementCreate`/`HtmlTextCreate` take the parent they link under — so RoboHTML has no
"detached node" state, and a DOM-created node is a *pending record* in the adapter whose linking is the
construction operation the gate controls.

That single fact arranges the whole adapter:

| Operation | On a parsed document | Why |
|---|---|---|
| query, read, `textContent` **read** | full | no construction involved |
| `setAttribute`, `getAttribute`, `removeAttribute`, `className`/`id`/`style` writes | full | `HtmlElementSetAttribute` is untroubled by finalization |
| `appendChild`/`insertBefore`/`removeChild` on nodes the document already has | full | Phase 8.4's stated purpose: finalization closes construction, not the tree |
| `textContent = "x"` where the lone child is already a text node | full | one `HtmlTextSet` — a **run rewrite**, not a construction |
| `textContent = "x"` where the children must be reshaped | **refused** | needs a text node created |
| `createElement`/`createTextNode`, then `appendChild` | **refused at the append** | linking a new node is construction |
| `document.title = "x"` | full if `<title>` already has a text child, else refused | the same rule through the same door |

### Three rules that are correctness, not polish

**1. The construction gate is asked *before* anything is destroyed.** An element's `textContent` write is "drop
every child, then create one text node". Doing the drop first and failing the create would delete the element's
text in order to report that it could not replace it — a *data-destroying answer to a refused write*. So the
shape is classified first: a lone text child is rewritten in place; anything else asks
`HtmlDocumentAllowsConstruction` and refuses *before* the first removal.
`js.integration.creation_refused` asserts the failure **and** that `"onetwo"` is still the `<ul>`'s text, which
is the assertion that matters.

**2. A wrapper follows its node through materialization.** When a pending record becomes real,
`JsDomRetargetWrapper` moves the interning entry from the synthetic id to the real one. Dropping the entry
instead leaves the script holding a wrapper whose id resolves to nothing — a bug that only shows up on the
*second* operation (`holder.appendChild(text)` after `document.appendChild(holder)`), which is why
`js.integration.creation_open` runs the whole create → attribute → append → text path and reads every node back
through `HtmlDocumentNodeById`.

**3. A refusal names its reason.** “appendChild refused the node” does not tell a caller the difference between a
malformed operand and a closed construction gate, so the refused cases throw
*"could not add a new node: this document accepts no new nodes"*. The DOM binding raises it where the reason is
known and keeps a host's own reason over the framework's generic "a property write failed" line.

### Counters

`JsDomHtmlGetCounters` reports the numbers the boundary turns on, each with one meaning (the header states them):
`created`, `pendingCreated`, `materialized`, `textNodesCreated`, `appends`, `attributeWrites`, `textWrites`,
`refusals`. `textNodesCreated` counts the text nodes the adapter caused to exist from `createTextNode` **and**
from a `textContent` write that had to build one — so "did that write create anything?" is a subtraction, and
`js.integration.text_in_place` is exactly that subtraction.

**Attribute writes are not structural.** RoboHTML's tree version tracks link changes, so an attribute write does
not move it; `attributeWrites` does. A consumer that cached attributes watches the second, one that cached a child
list watches the first. `js.integration.attributes` asserts both halves.

---

## 4. What is not built, and where it goes

- **A DOM construction gate that reopens after a parse.** The right fix is in RoboHTML's document-lifecycle work:
  let `RoboHtmlNodeOriginDom` register a node on a finalized document, keeping the parser's refusal intact. It is
  a one-condition change in `HtmlDocumentRegisterNode` — deliberately **not** made from Phase 16, because two
  Phase 8 suites pin the current answer as a decision. Until then, a page's script reads, mutates and rearrages,
  and a `textContent` write that reshapes an element is refused. `Limitations.md` carries the row.
- **`innerHTML` / `outerHTML`.** They need the parser to run *into* an existing tree, which is the same gate.
- **Mutation observers, `ShadowRoot`, `Range`, `Selection`, `cloneNode`, `document.importNode`.** Separate
  mechanisms with their own documents.
- **`fetch`, `XMLHttpRequest`, `WebSocket`, `localStorage`, `indexedDB`.** Host capability access, which
  `Security.md` keeps at zero.
- **CSSOM beyond the `style` facade.** `el.style` writes one attribute; a `getComputedStyle` needs RoboCSS and
  RoboLayout, not a binding.
- **`dataset`, `classList`, `children` as live collections.** Each is a small view over the same host doors, and
  each is a row of its own rather than a shortcut inside this one.
