# RoboJS — Event bindings (16.11)

An event is a **record**, a listener list is the adapter's, and nothing in this layer polls a device, owns a
window, or hit-tests. `RoboJSEvent.h` is the contract.

---

## 1. The record

```c
typedef struct JsEventRecord {
    char type[24];        /* "click", "pointerdown", "keydown", "input", "change", "focus", "blur", … */
    JsDomNodeId target;
    double clientX, clientY;
    int32_t button;
    uint32_t modifiers;
    char key[32];         /* "a", "ArrowLeft", … */
    int repeat;
    char data[128];       /* what an "input"-shaped event carries */
    uint64_t timestampMs;
} JsEventRecord;
```

**Fixed size, by value, no allocation, no pointer.** That is the whole threading story: a device thread can build
one in its own frame and hand it over (`Threading.md` §3), and a runtime can copy it into a task without owning
anything.

```c
Result JsEventRecordInit(JsEventRecord *record, const char *type, JsDomNodeId target);
Result JsEventQueue(JsRuntime *runtime, const JsEventRecord *record);      /* post; nothing runs */
Result JsEventDispatchNow(JsRuntime *runtime, const JsEventRecord *record); /* run now */
```

`JsEventQueue` is the door an input path uses: it appends a task and **nothing runs on the posting call**.
`js.integration.demo` asserts exactly that (`order.length` is 0 immediately after the post, and 1 after one
boundary) — it is the difference between registering and running, and the difference between an event system and a
callback call.

`JsEventDispatchNow` exists for a host that is already at a boundary and means "run it here" — a test, or a
synchronous `dispatchEvent`.

## 2. Listeners

```c
Result JsEventAddListener(JsRuntime *runtime, JsDomNodeId target, const char *type, const JsValue *listener);
Result JsEventRemoveListener(JsRuntime *runtime, JsDomNodeId target, const char *type, const JsValue *listener);
int32_t JsEventListenerCount(JsRuntime *runtime, JsDomNodeId target);
int32_t JsEventTotalListenerCount(JsRuntime *runtime);
int32_t JsEventDropListeners(JsRuntime *runtime, JsDomNodeId target);
int32_t JsEventDropAll(JsRuntime *runtime);
```

| Rule | Answer |
|---|---|
| a non-callable listener | `RoboInvalidArgument` — refused at registration, not at dispatch |
| the same function registered twice for one type on one target | **both** are kept, and both run, in registration order |
| listener order | registration order, per (target, type) |
| a listener that removes itself | the removal takes effect from the next dispatch; the current one finishes |
| `removeEventListener` for something never added | `RoboNotFound`, counted, not a silent success |
| `removeEventListener` for a function registered twice | removes **the first** entry with that exact function value, so a duplicate registration is removable one at a time |
| a listener that throws | **the dispatch ends** and the runtime's pending exception holds the throw — swallowing it would hide a script error behind a click |

The "both are kept" rule is the specification's: a DOM that de-duplicated listeners by function identity would
silently drop a page's second `addEventListener(f)`. The table is scanned linearly in registration order, so
"the listener added first runs first" is a fact about the table rather than about a hash — which is also what makes
`js.integration.steps`' two dispatches to one target see the same handler run twice.

## 3. The event object a script receives

A listener is called with `this` = the **current target's wrapper** and one argument: an object with `type`,
`target`, `currentTarget`, `clientX`, `clientY`, `button`, `modifiers`, `key`, `repeat`, `data` and
`timestampMs`, plus `preventDefault()` and `stopPropagation()`.

- `this` being the current target during the bubble phase is what makes a listener written as
  `function () { this.setAttribute(…) }` work, and it is why the dispatch passes the target wrapper rather than
  the record alone.- `preventDefault()` sets a flag stored in a small heap record the event object points at (`JsObject::hostData`),
  and the dispatch reports whether it was set. **A nested dispatch allocates its own record**, so an inner event's
  cancel cannot flip an outer one's. Nothing in the runtime *acts* on the flag — a host that wants "cancelled
  means do not scroll" reads it — which is the honest split: the module has no default action to cancel.
- `stopPropagation()` ends the bubble walk at the current target.

## 4. Propagation

A dispatch to a node walks **up the parent chain** from the target, running each ancestor's listeners for the type
in registration order. There is no capture phase and no `stopImmediatePropagation`; a `stopPropagation` ends the
walk. The tree comes from the DOM seam's `parent` callback, so the same dispatch works over the fixture and over
RoboHTML — which is what makes `js.event.binding` and `js.integration.steps` the same mechanism.

## 5. What a device adapter does

An input path — RoboInput, a window, a test — constructs a record and posts it:

```c
JsEventRecord record;
JsEventRecordInit(&record, "click", node);
record.clientX = 4.0;
record.clientY = 8.0;
JsEventQueue(runtime, &record);
```

Nothing in this layer knows what a pointer is, where a window is, or which node is under a point. **Hit testing is
the host's job**: a device adapter resolves a position to a node with whatever it already has (RoboInput's own
routing for a real shell), and this layer's contract starts at "a record naming a node".

**Named gap.** §16.11's literal requirement is that a RoboInput pointer event on a DOM element reaches a
JavaScript click listener. Today RoboDesktop's pointer path delivers a `RoboWindow` event and the RoboInput →
desktop wiring that would turn it into a *node-targeted* record is Phase 13's still-open shell integration. The
path that is proved end to end here is `JsEventRecord` (posted by an adapter) → `JsLoopRunOne` → the listener,
with the DOM write read back out of RoboHTML by `js.integration.demo`. `Limitations.md` carries the row, and the
one-line shape of the fix: a desktop adapter that resolves the point and posts a record.

## 6. What is absent

Capture phase (`capture` is not a listener option), `stopImmediatePropagation`, `bubbles`/`cancelable`,
event classes and `CustomEvent` (an event is a plain object, so a script can build one and `dispatchEvent` it),
`once`/`passive`, listener de-duplication, a `defaultPrevented` *property* (the flag is read through the
`preventDefault` path and reported by the dispatch rather than exposed as a property), event delegation helpers,
focus and pointer-capture semantics, and a device poller of any kind.
