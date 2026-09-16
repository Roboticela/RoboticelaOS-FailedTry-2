# RoboHTML Test Organisation (Phase 8.15)

Phase 8.15 adds no product behaviour. It adds the test layer that a phase-by-
phase suite cannot express: the assertions that only exist *between* the phases.

## 1. Why a second kind of suite exists

There is one test framework in the runtime (`runtime/Tests/Framework`, the
`ROBO_CHECK*` macros and `robo_test_register`) and this phase does not add
another. What it adds is a suite whose *subject* is different.

The per-phase suites each own one surface and are written by the phase that
built it: `html.element.*` pins the element model, `html.attr.*` the attribute
model, `html.form.*` the form model. Every one of them can pass while the model
as a *system* is wrong, because none of them is positioned to ask a question
that spans two surfaces:

| Question | Owner |
|---|---|
| Does an attribute change move the image state? | 8.6, 8.12 |
| Does the fragment lookup and a label's `for` agree? | 8.13, 8.14 |
| Does an `id` rename reach every consumer at once? | 8.13, 8.14 |
| Can a node from one document reach another? | 8.3, 8.9 |
| Does `<input>` accept children? | 8.3 (parser) vs 8.9 (mutation) |
| Do malformed inputs still audit clean? | 8.3, 8.9 |
| Does repeated document lifetime leak? | all of them |

`SuiteRoboHTMLDom.c` (`html.dom.*`) is where those questions live. It is the
only suite that is *allowed* to be cross-cutting, and the 13 tests in it are
deliberately shaped as invariants rather than as per-API checks.

## 2. Suite map

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboHTML.c` | 5 | Module init, version, types, resource slot |
| `SuiteRoboHTMLDocument.c` | 6 | Document creation, source attachment, hooks |
| `SuiteRoboHTMLDocumentModel.c` | 23 | Document model, root ownership, tree invariants |
| `SuiteRoboHTMLParser.c` | 5 | Parser entry points, acceptance |
| `SuiteRoboHTMLHooks.c` | 6 | Mutation/lifecycle hooks |
| `SuiteRoboHTMLToken.c` | 22 | Token vocabulary and lifetime |
| `SuiteRoboHTMLCharRef.c` | 9 | Character references |
| `SuiteRoboHTMLTokenizerStates.c` | 9 | Tokenizer states, raw-content switch |
| `SuiteRoboHTMLTreeBuilder.c` | 34 | Tree construction, recovery, dump |
| `SuiteRoboHTMLElement.c` | 20 | Element model |
| `SuiteRoboHTMLElements.c` | 22 | The 32 element types through the parser |
| `SuiteRoboHTMLAttribute.c` | 21 | Attribute model |
| `SuiteRoboHTMLText.c` | 23 | Text model |
| `SuiteRoboHTMLTraversal.c` | 20 | Traversal, ordering, walkers |
| `SuiteRoboHTMLMutation.c` | 34 | Structural mutation |
| `SuiteRoboHTMLLifecycle.c` | 26 | Document lifecycle graph |
| `SuiteRoboHTMLTag.c` | 18 | Tag table and predicates |
| `SuiteRoboHTMLImage.c` | 15 | `<img>` resource model |
| `SuiteRoboHTMLAnchor.c` | 27 | `<a>` navigation model |
| `SuiteRoboHTMLForm.c` | 25 | Form control model |
| **`SuiteRoboHTMLDom.c`** | **13** | **Cross-cutting invariants (this phase)** |

`SuiteRoboHTMLPhase8.c` (3) is the phase-acceptance roll-up. `SuiteRoboDemoHtml.c`
is not in this directory: the visual checkpoint belongs with the demo, and
`html/README.md` documents its filters.

`html.*` is 386 tests of the runtime's 1407.

## 3. The DOM suite

| Test | The one thing it pins |
|---|---|
| `html.dom.document_isolation` | Two documents that parsed identical bytes stay two documents: every cross-document operation is refused, and the unaddressed document is verified unchanged. |
| `html.dom.id_resolution_agrees` | The fragment lookup, a label's `for` and a control's `form` are three consumers of one rule, asserted to agree — including where they agree on *nothing*. |
| `html.dom.id_change_propagates` | Renaming an `id`, retargeting a `for`, renaming a form, and removing an `id` each move every consumer on the next call: nothing is cached. |
| `html.dom.void_child_behaviour` | Void is a *parser* rule, not a DOM one: the tree builder never puts a child under `<input>`, while the mutation API decides by node *kind* and allows it. Both facts asserted, because a reader will assume one is the other. |
| `html.dom.invariants_after_recovery` | A malformed corpus leaves a tree that audits clean and re-parses byte-identically. |
| `html.dom.rebuild_is_deterministic` | Building the same source twice produces the same dump, node count and tree shape. |
| `html.dom.attribute_mutation_sequences` | Set → read → replace → remove with dependent state (anchor view, image state, declared/current control value) checked at every step. |
| `html.dom.deep_structure_stress` | 200 levels: order, depth, a whole-subtree move, and teardown. |
| `html.dom.wide_structure_stress` | 300 siblings: order, insertion at both boundaries, bulk removal. |
| `html.dom.unicode_matrix` | Thirteen script families through parse, attribute, text node, mutation and the shared support helpers. |
| `html.dom.unicode_id_consumers` | The same thirteen families' ids through the three consumers, including a one-byte near miss. |
| `html.dom.lifecycle_churn` | 24 document lifetimes with the debug allocator's live count returning exactly to baseline. |
| `html.dom.feature_integration` | One document that uses every subsystem, checked as a whole, then mutated and torn down. |

Three of them are the ones to read first, because they are the ones that would
fail if the model were quietly wrong: `id_resolution_agrees`,
`void_child_behaviour`, `attribute_mutation_sequences`.

### The capability flags

`feature_integration` asserts the flags `robo_html_dom_query_capabilities`
reports. `dom_nodes` is asserted to be **0**, and that is not a gap: it is the
flag for a *complete* node DOM surface, and this build has one model per aspect
(attributes, text, traversal, mutation) rather than the whole. `events` is 0 for
the same reason — its phase has not run. A test that asserted 1 for either would
be the one lie in an otherwise verifiable set.

## 4. Fixtures: parsed versus hand-built

The support header (`runtime/html/tests/RoboHTMLDomTestSupport.h`) is not a
framework. It adds no runner, no registration, no reporter and no assertion
vocabulary — everything is `static inline`, so a suite pays only for what it
calls and no unused-function warning appears under `-Wall -Wextra -Werror`.

The one rule it encodes is that **the default fixture parses real HTML through
the real tokenizer and tree builder**. A helper that hand-built nodes would let a
suite pass while the parser integration it claims to test was broken. So:

- `dom_document_of(text)` → a new document with `text` parsed into it.
- `dom_find_tag` / `dom_find_id` / `dom_find_by_attr` → the runtime's own
  descendant walk with a predicate, in document order, so a lookup in a test
  answers the way a lookup in the runtime does. `dom_find_by_attr` matters where
  an `id` is ambiguous: an `id` lookup returns the first element carrying it,
  which is exactly what the consumer rule says and sometimes not the element the
  test means.
- `dom_expect_clean_tree(id)` → the runtime's own whole-tree audit
  (`robo_html_document_validate_tree`), asserting `ok`, zero orphans, zero bad
  links, zero cycles, zero bad parents, zero bad counts and zero sibling
  mismatches. Detached subtrees are legal and do not make the audit unclean.
- `dom_text` / `dom_dump` treat their buffer capacity as a hard ceiling and
  fail on truncation: an assertion about Unicode preservation that silently
  compared a prefix would pass for the wrong reason.
- `dom_unicode_samples()` → the thirteen families once, shared by both Unicode
  tests so the matrix and the id consumers cannot drift apart.

Node **construction** is the one case that cannot use the parser: the
construction seam is gated on a document that has not been finalized, and a
parsed document is finalized by definition. Those tests build a document by hand
and use the same support helpers for traversal and auditing, so parsed and
hand-built trees are checked by the same code.

## 5. Running the tests

Focused DOM suite (the substring filter matches test *names*):

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "html.dom"
```

Whole RoboHTML surface:

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "html."
```

Full runtime suite (the authoritative check):

```powershell
.\scripts\robo.ps1 test-runtime
```

The pipeline equivalent of the focused run, which also records the result:

```powershell
.\scripts\robo.ps1 test-runtime --filter "html.dom"
```

## 6. Sanitizers

The host build already had a `-Sanitize` switch; this phase used it rather than
adding a second mechanism. It probes the toolchain, enables what exists, and
fails the run if it enabled sanitizers and the build then did not use them.

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Sanitize -Filter "html.dom"
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Sanitize
```

Result: **AddressSanitizer and UndefinedBehaviorSanitizer both enabled, 1407
tests, 1407 passed, zero findings.**

### What the sanitizers found

Two real defects, both in the payload arena's address arithmetic, both fixed
rather than suppressed:

1. **Misaligned payload headers** (`RoboHTMLPayload.c`, `RoboHTMLNode.c`,
   `RoboHTMLDocument.c`). The arena is a bump allocator over a byte cursor, so a
   reservation that followed an odd-length name began at an odd offset — and a
   payload header is four `uint32_t`s, which is undefined behaviour to access
   unaligned. UBSan reported it on every element write. Fixed by aligning the
   cursor to `ROBO_HTML_PAYLOAD_ALIGNMENT` (the header's own required alignment)
   before each reservation. The padding is unreachable through any
   offset/length pair, and the reservation still ends exactly at the new
   `payload_used`, so the in-place tail-growth path is unchanged.

2. **`NULL + 0` in reference trimming** (`RoboHTMLAnchor.c`). An absent `href` is
   NULL, and `href + begin` computes a null-pointer offset even when `begin` is
   0. Fixed by keeping the trimmed reference NULL when the input is NULL.

Neither was introduced by this phase; both had been latent since the arena and
the anchor model were written, and both are invisible without UBSan.

## 7. What this phase did not add

No CSS, no selectors, no cascade, no layout. No events, no listeners, no
`querySelector` surface. No JavaScript. No form submission, validation or
encoding. No networking, no image loading. No second test framework, runner or
assertion vocabulary. No weakening or removal of an existing test.

The only product-code changes in this phase are the two sanitizer fixes above.
