# RoboHTML

RoboHTML is the runtime's HTML parser and DOM. It owns one tokenizer, one tree
builder and one document model, and it deliberately owns nothing above them: it
opens no file, speaks no network, decodes pixels only through a caller-supplied
resolver, and evaluates no script. The one file in the tree that evaluates a
script is RoboJS's, and its DOM bindings cross into this module through the
same doors the shell uses.

This document is the module's front door. Phase-specific contracts live beside
it:

| Phase | Contract |
|---|---|
| 8.1–8.13 | the tokenizer, tree builder, document model, element/attribute/text model, traversal, mutation, tags, image resource, anchors |
| 8.15 | `DomConformance.md`'s sibling suites (`html.dom.*`) and the shared fixtures in `Tests/Html/RoboHTMLDomTestSupport.h` |
| 17.1–17.9 | the DOM batch: construction/lookup, forms, control state, navigation, image lifecycle, tables, templates, shadow DOM, custom elements |
| 17.10 | `RoboHTMLSecurity.h` — the posture table and the capability set |
| 17.11 | `DomConformance.md` — the cross-surface conformance matrix |
| 17.12 | the end-to-end integration suite (`html.integration.*`) and this document |
| 17.13 | the script side of the same doors: the `js.integration.*` cases in `SuiteRoboJSIntegration.c` |

## Ownership

| Surface | Public header | What it is |
|---|---|---|
| Module | `RoboHTML.h` | init/shutdown, version |
| Source | `RoboHTMLSource.h` | bytes plus a declared encoding label; no I/O |
| Tokenizer/builder | `RoboHTMLTokenizer.h`, `RoboHTMLTreeBuilder.h` | source → tokens → tree |
| Document | `RoboHTMLDocument.h` | lifecycle, root, whole-tree audit, reset |
| Node | `RoboHTMLNode.h` | handles, kinds, views, identity |
| Element | `RoboHTMLElement.h` | names, tags, attributes, name policy |
| Traversal | `RoboHTMLTraversal.h` | relations, walks, bounded search |
| Mutation | `RoboHTMLMutation.h` | append/insert/remove/replace, tree version |
| DOM query | `RoboHTMLDomQuery.h` | create, clone, textContent, getElementById/Tag/Class, selector seam |
| Tags | `RoboHTMLTag.h` | the recognised element vocabulary and its roles |
| Forms | `RoboHTMLForm.h`, `RoboHTMLFormTree.h` | typed attributes, ownership, collection, validation, submit/reset |
| Controls | `RoboHTMLInput.h` | value/checked/selection state and the radio rule |
| Links | `RoboHTMLAnchor.h`, `RoboHTMLNavigation.h` | reference → target, scheme policy, the document controller |
| Images | `RoboHTMLImage.h`, `RoboHTMLImageLoad.h` | resource state, the fetch seam, the load lifecycle |
| Tables | `RoboHTMLTable.h` | rows/sections/cells/indexes; no grid |
| Templates | `RoboHTMLTemplate.h` | inert content, cloning, fragment insertion |
| Shadow DOM | `RoboHTMLShadow.h` | open roots, the boundary, retargeting |
| Custom elements | `RoboHTMLCustomElements.h` | per-document registry, callbacks, upgrade |
| Security | `RoboHTMLSecurity.h` | posture, capabilities, the lifetime report |
| Diagnostics | `RoboHTMLDiagnostics.h` | domains and `HtmlDiag` |

## Dependency direction

```text
RoboBase → RoboMem → RoboContainers → RoboUnicode / RoboI18N
                    ↘ RoboFont → RoboGraphics → RoboHTML
RoboHTML → RoboJS(dom seam) → the shell or document layer that runs a page's script
```

RoboHTML may include the base layers, RoboUnicode/RoboI18N, RoboFont and
RoboGraphics — the image path decodes through `GfxImageDecode`, which is why
RoboGraphics is a real dependency and not merely a header. It may include
**nothing above it**: no RoboCSS, no RoboLayout, no RoboPaint, no RoboRender, no
RoboWindow or RoboDesktop. The selector engine is the case that proves the rule
works in practice: `querySelector` is an installed matcher seam that RoboCSS
satisfies, so the dependency runs CSS → HTML and never the reverse.

## The rules this module keeps

- **No I/O.** Bytes arrive through a caller-supplied seam — an image resolver,
  a source buffer, a navigation handler. Nothing here opens a path or a socket.
- **No script.** There is no `<script>` execution, no event listener registry
  and no timer. A handler attribute is data the DOM stores verbatim.
- **No second text engine.** Text is carried as bytes and handed to RoboText by
  the layers above; the DOM does not decode UTF-8 beyond what a name or an id
  comparison needs.
- **No cache that can go stale.** `form.elements`, validity, the form owner, a
  template's inertness, a shadow boundary and a node's reachability are all
  answered by asking the tree at the moment of the question. A mutation cannot
  leave an association behind because there is no association stored.
- **One version counter.** `HtmlNodeTreeVersion` is the monotonic fact a
  consumer uses to notice that a structural change happened; it is not a
  per-node invalidation flag and nothing here tries to be one.
- **Failure is a state, not an exception.** A bad reference, a missing asset, a
  refused scheme and an unreachable template content are all *answers*; only a
  structural misuse is an error code.

## Capabilities and the security posture

`HtmlDocumentCapabilities` narrows five doors — DOM mutation, resource loading,
navigation, custom elements, shadow DOM — and a refused door answers
`RoboPermissionDenied`. The gates sit at the doors a *document* can reach, not
inside the primitives the parser uses, which is what lets a host parse untrusted
markup and refuse every later mutation of it. `HtmlSecurityGetPosture` is the
module's own statement of what the DOM cannot reach, field by field.

The DOM has no sandbox, no origin, no CSP, no Trusted Types and no URL
sanitizer. Those are named absences in `RoboHTMLSecurity.h`, not gaps to be
filled quietly.

## Versioning and API stability

`RoboHTMLVersion.h` pins the module. The surface is still growing — Phase 19+
and the integration phases add consumers, and the freeze is Phase 39's
(`RoboHtmlApiFrozen` is 0, and it is asserted as 0 in `SuiteRoboHTML.c`).

Two conventions a change must follow:

- **A surface change moves the minor version, and the two assertions in
  `SuiteRoboHTML.c` move with it in the same commit.** `RoboHtmlVersionString`
  is the one string every capability record reports, so a second spelling is a
  defect.
- **A new capability record is a new table read field by field in
  `SuiteRoboHTMLDomConformance.c`.** A field that is neither 0 nor 1 is refused
  by the door that owns it rather than read as truthy.

## Tests

| Suite | Filter | What it proves |
|---|---|---|
| `Tests/Html/*` | `html.` | every phase surface against its own contract |
| `SuiteRoboHTMLDomConformance.c` | `html.conformance.` | the joins between surfaces, the capability matrix, the audit, determinism |
| `SuiteRoboHTMLDomIntegration.c` | `html.integration.` | one realistic page through every surface end to end |
| `SuiteRoboJS` DOM cases | `js.dom.` | the script side of the same doors |

```
pwsh -NoProfile -File Scripts/Test/TestHtml.ps1 -Target Host
pwsh -NoProfile -File Scripts/Test/TestRoboJS.ps1 -Target Host
pwsh -NoProfile -File Runtime/Tools/Build.ps1 -Module Desktop -Verify
```

## Named absences

`innerHTML`/`outerHTML`, a selector engine inside this module, form submission
networking and `pattern` validation, `srcset`/`<picture>`, image load events and
asynchronous loading, table grid layout, the "in template" insertion mode,
declarative shadow DOM, slotting and `:host`/`::slotted`, custom element
constructors and a reaction queue, customised built-ins, scoped registries,
closed shadow roots, events of any kind, and a serializer. Each is stated in its
own header's capability table and asserted as a `0` in
`SuiteRoboHTMLDomConformance.c`.
