# RoboHTML Links (Phase 8.13)

This document is the design record for Phase 8.13. It is normative for
`RoboHTMLAnchor.h` / `RoboHTMLAnchor.c` and for the demo adapter's link
painting, hit-testing and activation.

Phase 8.11 already gave `a` a tag identity and a text-level role, so the parser
was building anchors before this phase began. What was missing is everything
*after* the element: an `<a>` is only interesting because of what its `href`
names.

```text
<a href="/Settings" target="_self" rel="noopener" download>Settings</a>
     │
     ├── attributes   robo_html_element_anchor_attributes
     │      href       what to navigate to
     │      target     which context should receive it
     │      rel        preserved; queryable, never interpreted
     │      download   a save *intent*, not a save
     │      title      advisory text, kept verbatim
     │
     ├── target       robo_html_navigation_target_t (owned, resolved)
     │      kind       NONE │ ROUTE │ FRAGMENT │ EXTERNAL │ INVALID
     │      resolved   the normalized path, or the URI verbatim
     │      fragment   the `#id` part, if there was one
     │      scheme     the URI scheme, if there was one
     │
     └── activation   robo_html_element_activate
            └── request   robo_html_navigation_request_t
                  └── handler    the delegated-execution seam
                        └── outcome   robo_html_navigation_outcome_t
```

## 1. There is no browser here, so there is no browser security model

RoboticelaOS is an application runtime. It has applications and windows, not
origins, cookie jars, tabs or a same-origin policy — and nothing in this model
would use any of them, so none of them is invented. A document belongs to an
application, and navigation is a request the application's own router either
takes or does not. What a browser expresses through cookies and sessions,
RoboticelaOS expresses through application identity and the capability model it
already has. This phase therefore adds **no security model of its own**. What it
does add is refusal, with a reason.

That refusal is the phase's central statement, and it lives in
`robo_html_navigation_scheme_is_permitted`:

| Scheme | Answer | Why |
|---|---|---|
| `http`, `https`, `mailto`, `tel` | permitted | the seam may see it |
| `javascript:` | refused | an anchor must not run script |
| `data:` | refused | this runtime does not decode it |
| `file:` | refused | a document has no filesystem capability, and an attribute cannot grant one |
| anything else | refused | unrecognised, so there is nothing to delegate to |

Two questions are deliberately separate, because they answer different things:
`robo_html_navigation_scheme_is_known` says whether the runtime understands a
scheme at all, and `..._is_permitted` says whether it may be handed to a
handler. `file` is known and not permitted — "we understand what you asked for
and we refuse it" — while `weird` is neither. A refused scheme never reaches the
handler: the check happens before the seam is consulted.

*Permitted* is not *reachable*. With no handler installed even `https` reports
`UNSUPPORTED`, because the stack that would serve it is a later phase. This
function only says the seam may see it.

## 2. Nothing here performs I/O

The 8.1 boundary said RoboHTML never acquires bytes, and 8.12's image model kept
it. This is the same shape one level up: a caller installs a
`robo_html_navigation_handler_t`, and *that* decides what an accepted route
means.

Two consequences are load-bearing:

- **An activation never reports success for navigation that did not happen.**
  The outcome vocabulary has no `NAVIGATED` kind and will not have one until
  something can actually navigate. A route with no handler is `UNSUPPORTED`,
  which is the truth.
- **A scheme the runtime does not permit never reaches the handler.** There is
  no path by which an anchor reaches a subsystem the document was never granted.

## 3. Fragments resolve locally, because they need no I/O

`#Overview` names an element in *this* document, and finding one is a tree walk
— so a fragment activation is answered here rather than delegated, and reports
`FRAGMENT` or `FRAGMENT_MISSING`. That is the one navigation this phase can
complete honestly, and the demo checkpoint shows it completing.

`robo_html_navigation_find_fragment` is deliberately named for its caller and is
**not** `getElementById`: the id lookup every DOM query will need is a later
phase's API, with its own name, its own caching question and its own error
surface. What exists here is what a fragment activation needs — first match,
document order, exact case-sensitive comparison, the id *attribute* rather than
node identity. An empty fragment (`href="#"`) names the top of the document and
therefore names no element, so it is `NOT_FOUND` by definition.

## 4. Resolution is small, and total

`robo_html_navigation_target_create` parses and resolves one reference against
the document's own reference (`robo_html_document_source_name`):

| Reference | Kind | Resolved |
|---|---|---|
| *(absent / empty / whitespace)* | NONE | — |
| `/Path` | ROUTE | `/Path` |
| `Path`, `./Path` | ROUTE | `<base dir>/Path` |
| `../Path` | ROUTE | one segment popped from the base dir |
| `Path#Fragment` | ROUTE | route, with `fragment` set |
| `#Fragment` | FRAGMENT | the base reference |
| `scheme:rest` | EXTERNAL | the URI verbatim |
| unparseable, or > 4 KiB | INVALID | — |

A `..` that would escape above the root is clamped to the root, matching what a
URL does; it is not an error. A document with no name is a normal in-memory
document, not an error, so `x` stays `x`. A query string is carried verbatim and
not broken out into a field, because nothing in this phase reads it.

What is deliberately **not** here: percent-encoding, path case folding, IDNA.
Those belong to a URL library, and half-implementing them would be a behavioural
claim this phase cannot back. There is no second URL parser either — this is the
runtime's reference resolver, and the `href` bytes are kept verbatim next to the
resolution so a caller can always see what the document actually wrote.

Failure is a *kind*, not an error: an unusable reference yields `ROBO_OK` with
kind `NONE`, an unparseable one `ROBO_OK` with kind `INVALID`. That is why
resolution is a pure function of its arguments and works with no document at
all — which is also why activation is the only part that has to care about
document lifetime.

## 5. The activation order

```
1. refuse anything that is not a live <a>
2. read href, resolve it against the document's own reference
3. build the request: target, context, rel, download intent, title
4. decide, without consulting anything, whether there is navigation to do:
     no usable href        → NO_TARGET
     unparseable reference → REJECTED
     download intent       → DOWNLOAD_PENDING   (saving needs a filesystem
                                                 capability this runtime lacks)
5. a fragment is resolved HERE, in this document → FRAGMENT / FRAGMENT_MISSING
6. an external URI whose scheme is not permitted → UNSUPPORTED, without
   consulting the handler (the security rule)
7. otherwise ask the handler; its refusal is UNSUPPORTED, its acceptance keeps
   the kind. With no handler installed, a route or a permitted URI is
   UNSUPPORTED — the honest answer
```

`ROBO_OK` accompanies every one of those outcomes, including the refusals: they
are answers, not errors. Errors are reserved for things that are not answers at
all (`ROBO_NOT_FOUND` for a dead handle, `ROBO_NOT_SUPPORTED` for a non-anchor,
`ROBO_INVALID_ARGUMENT`, `ROBO_OUT_OF_MEMORY`).

**The outcome owns the target it reports.** That is the only shape that works:
activation resolves the reference itself, so if the outcome merely borrowed, the
target would die at the end of the call and every reported string would dangle.
Instead the target is handed to the outcome, the convenience pointers borrow
from it, and `robo_html_navigation_outcome_clear` releases it — one clear rule
with no lifetime trap. An outcome that was never filled is safe to clear, so
"clear what you were given" is always valid on a failure path.

Every borrowed pointer in the request lives only for the duration of the handler
call. That is stated once, and it is what keeps the seam from having to acquire a
lifetime contract.

## 6. `target`, `rel`, `download` — preserved and classified, not implemented

`target` is classified (`CURRENT`, `NAMED`, `NEW`, `PARENT`, `TOP`, `UNKNOWN`)
and nothing is created: RoboticelaOS has applications and windows, not a tab
strip, so `_blank` is a distinct context a future window/application layer may
act on, and collapsing it to `CURRENT` would throw away the information that the
document asked for a new context.

`rel` is *preserved and answered*, never interpreted. `nofollow`, `noopener`,
`noreferrer` and `external` all change what a browser's network layer does and
mean nothing to a runtime with no network, so inventing consequences here would
be inventing behaviour. `robo_html_link_rel_contains_token` answers "does this
link ask for X?" as a whole ASCII-case-insensitive token, and `rel` travels on
the request so a later phase can act on it without this API changing.

`download` is an intent, not a save: it is preserved, reported on the request,
and an activation that would need to save something returns
`DOWNLOAD_PENDING`. It does not bypass filesystem capability checks, and there
is no download manager.

## 7. The demo checkpoint is temporary adapter behaviour

The demo adapter paints a link in the brand colour with a hairline rule under
the run, keeps the `href` bytes verbatim, and hit-tests it. That exists so the
pointer → hit test → anchor → activation chain can be *seen* end to end in the
host window and in QEMU; it is not a style engine, and no other part of the
runtime may infer a visual contract from it. Styling a link is CSS's job in a
later phase.

Two limits are deliberate and asserted rather than left to be discovered:

- **A link nested in a paragraph is painted as part of that run.** The adapter
  has no inline flow, and a block's text is its whole subtree, so only a
  block-level link becomes its own hit-testable block. Painting it any other way
  would mean growing an inline layout model inside the one module that must not
  have one.
- **A page built from markup owns the document it built**, because the HTML it
  was handed is gone and a link activation needs a tree. A page built from a
  caller's document borrows it and never destroys it. One rule per constructor,
  so "who owns the document?" has one answer per call rather than one per
  caller.

## 8. What this phase is not

CSS, link styling as a permanent rule, layout, click or event DOM APIs, the
History API, Fetch, HTTP, WebSocket, cookies, CORS, service workers, a download
manager, browser tabs or windows, or a native protocol launcher. Cookies,
sessions and CORS are not merely unimplemented here — the model they would need
is absent on purpose.
