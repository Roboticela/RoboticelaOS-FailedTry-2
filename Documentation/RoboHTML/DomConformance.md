# RoboHTML DOM conformance (Phase 17.11)

Phase 17.1–17.10 each added a surface and each has a suite that proves that
surface against its own contract. Those suites cannot see the places where two
surfaces must agree. This document is the contract of the suite that can:
`Tests/Html/SuiteRoboHTMLDomConformance.c`, registered as
`html.conformance.*` (11 cases).

The suite adds no production surface. Every number it asserts is read off a door
that already existed, and every document it builds is parsed through the real
source → tokenizer → tree-builder path (`DomDocumentOf`).

## 1. What it asserts, and why each is a join

| Case | The join it proves |
|---|---|
| `version` | One version string, five readers. Five capability records carry `version`; a bump that missed one would otherwise be invisible. |
| `capability_matrix` | Every field of every 17.x capability table is exactly `0` or `1`, and the fields the phase briefs name as absences are absences. Reads nine tables field by field. |
| `reference_policy` | An anchor's scheme policy and an `<img>`'s reference classification are the *same* policy. A second scheme list would let a reference be refused in a link and fetched in an image. |
| `form_collection` | `form.elements` is the DOM's rule — explicit `form` association, then the nearest ancestor — not a descendant walk. Includes a control that names the form from outside it, a control of another form, and a move. |
| `table_structure` | Rows and cells are counted in document order across sections, `colspan` is read but not applied, and a detached row is not a row *of a table*. |
| `template_inertness` | A parsed template's children live in its content fragment and are unreachable from the document root, proved by a document-scoped lookup that must miss; cloning produces a live tree without making the template reachable. |
| `shadow_boundary` | A shadow root's nodes are ordinary nodes that a document-scoped lookup cannot see, the host's light children are untouched, `closed` is refused, and a second root is refused rather than replaced. |
| `custom_element_lifecycle` | Callbacks follow the **tree's shape**: define upgrades a parsed element (observed attribute first, then connection), an unobserved attribute is silent, a detached subtree is silent, and connecting the holder connects the element. `Malformed` and `unavailable` names are different answers. |
| `security_gates` | Every capability gate answers `RoboPermissionDenied` — never `RoboNotSupported` — and the same six doors answer `RoboNotFound` on a destroyed document. Refusals change nothing. |
| `lifetime_audit` | After every 17.x surface has been exercised on one document, the runtime's own whole-tree audit is clean, and a shadow root and a template's content are legal detached roots rather than corruption. |
| `determinism` | One markup, built 101 times, produces a byte-identical tree dump (`HtmlTreeDumpToBuffer`) each time. |

## 2. The capability tables, as read

`capability_matrix` is the machine-readable form of what Phase 17 ships. The
zeros are the interesting half:

| Table | 1 (present) | 0 (stated absent) |
|---|---|---|
| `HtmlDomCompletionCapabilities` | creation, cloning, textContent, prepend, lookupById/Tag/Class, selectorSeam | selectorEngine, innerHtml |
| `HtmlFormTreeCapabilities` | association, elements, validation, submit, reset | patternValidation, submissionNetworking |
| `HtmlInputCapabilities` | textEditing, checkbox, radioGroups, stepping | caretModel, eventDispatch |
| `HtmlTableCapabilities` | structure, indexes, spansRead, caption | spanLayout |
| `HtmlNavigationCapabilities` | elementHref, controller, fragmentNavigation, locationState, documentLoad | preventDefault, networking |
| `HtmlTemplateCapabilities` | contentFragment, parserAdoption, inertContent, cloning, fragmentInsertion, setContent, inertnessQuery | insertionMode, declarativeShadowDom, scriptExecution |
| `HtmlShadowCapabilities` | attachShadow, shadowRootQuery, hostQuery, lightDomPreserved, boundaryLookup, scopedLookup, eventRetargeting, focusBoundary, reachabilityQuery | closedMode, slotting, hostSelectors, declarativeShadowDom, secondRenderingTree, hostCandidates |
| `HtmlCustomElementCapabilities` | registry, nameValidation, definedQuery, definitionQuery, createElement, connected/disconnected/attributeChanged callbacks, upgradeOnDefine, reachabilityDriven | constructors, reactionQueue, customisedBuiltins, scopedRegistries, definedSelector, callbackResults |
| `HtmlImageLoadCapabilities` | fetchSeam, intrinsicSize, declaredSize, referencePolicy, layoutInvalidation, replacement, removalAbandon | srcset, responsiveImages, loadEvents, asyncLoading, networkAccess |
| `HtmlSecurityPosture` | bytesOnlyThroughSeam, hostOwnedDocument | filesystem, network, nativePointers, processes, hardware, hostEnvironment, scriptExecution, eventListeners |

A field flipped from `0` to `1`, or from `1` to `0`, is a change to this table
and to `SuiteRoboHTMLDomConformance.c` in the same commit.

## 3. Deliberately not asserted here

- **No new fixture and no rendered output.** The DOM is a model; the visual
  half of the round is the shell's, and the shell's own conformance is Phase
  15.18's.
- **No selector engine.** `querySelector` is an installed *seam*
  (`RoboCssDomSelect` is the matcher); a conformance case over it lives with the
  module that implements the matcher, not here, and `selectorEngine` is 0.
- **No `innerHTML`.** Reading markup back out needs the serializer, which the
  parser phase names as a gap; the matrix asserts the absence instead of
  inventing a case.
- **No events, no script, no networking.** `eventListeners`, `scriptExecution`
  and `networkAccess` are 0 by design, and the one seam that could reach outside
  the process — the image fetch — is proved to be consulted only when the
  document has the capability.

## 4. How to run

```
pwsh -NoProfile -File Scripts/Test/TestHtml.ps1 -Target Host
```

That runs all 506 `html.*` cases, `html.conformance.*` among them. A change to
any 17.x surface is not done until `TestHtml.ps1 -Target Host` passes, and a
change that moves the version pin is not done until `SuiteRoboHTML.c`'s two
version assertions are updated in the same commit.
