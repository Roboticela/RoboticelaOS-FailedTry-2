# RoboCSS Specificity (Phase 9.6)

Specificity is the third and last of the cascade's ordering keys: it belongs to the
**selector that matched** rather than to where a declaration was written, and it is
what separates two declarations that share an origin, an importance and a level.

Phase 9.5 shipped the cascade without it and answered `UNDECIDED_SPECIFICITY`
rather than guessing. Phase 9.6 supplies it, and that verdict is gone — the cascade
now decides every property question by:

```
level  →  specificity  →  document order
```

| Key | Owned by | Status |
|---|---|---|
| origin and importance — the *level* | 9.5 | complete, final |
| **specificity** | **9.6** | complete |
| the document order of the declarations | 9.5 | complete |

## The representation

```c
typedef struct robo_css_specificity {
    uint32_t id;          /* A: id selectors                        */
    uint32_t class_like;  /* B: classes, attributes, pseudo-classes */
    uint32_t type_like;   /* C: types and pseudo-elements           */
} robo_css_specificity_t;
```

Field order **is** comparison order. A specificity is a tuple, compared
lexicographically: A first, then B, then C. `#app` is `(1,0,0)`, `.panel` is
`(0,1,0)`, `div` is `(0,0,1)`, and that is why `#app > .panel > div` ranks them.

Three fields rather than one packed integer, and the reason is the failure mode a
packed integer has. A single number has to choose a radix, and any radix below the
maximum a component can reach silently *carries*: with a radix of 1000, `(0, 1000,
0)` and `(1, 0, 0)` become the same number, and the answer is wrong in a way that
looks exactly like an answer. Three fields cannot make that mistake, and there is
exactly one place the ordering is written down:

```c
int robo_css_specificity_compare(const robo_css_specificity_t *a,
                                 const robo_css_specificity_t *b);  /* -1, 0, 1 */
```

A caller must not compare the fields itself. `robo_css_specificity_equal` is a
convenience over the same function, and `robo_css_specificity_is_zero` exists
because "no selector, so nothing" is a real value here.

### Saturation, not wrap

Each component saturates at `ROBO_CSS_SPECIFICITY_COMPONENT_MAX` (`UINT32_MAX`)
instead of wrapping. The bound is unreachable from a parsed stylesheet — it would
take 2³² id selectors in one selector — but a hand-built or generated selector is
bounded by nothing, and `uint32_t` arithmetic that *wrapped* would turn a selector
with more ids than the maximum into one with fewer. That is the one arithmetic
failure that inverts the comparison rather than merely losing precision, so the
accumulators saturate: `robo_css_specificity_add` and
`robo_css_specificity_take_max` clamp at the maximum, and "more" keeps meaning
"more". `css.specificity.saturation` pins both directions.

## What each thing is worth

| Selector | Contribution | Note |
|---|---|---|
| `div` | C + 1 | a type selector |
| `*` | nothing | the universal selector is worth zero wherever it sits |
| `.card` | B + 1 | |
| `#main` | A + 1 | each *syntactic* id counts, so `#a#b#c` is `(3,0,0)` |
| `[disabled]`, `[a=b]`, `[a^="b"]` | B + 1 | the operator is how the attribute is tested, not another selector |
| `:root`, `:first-child`, `:nth-child(…)` | B + 1 | the An+B argument is part of the pseudo-class |
| `::before`, `::first-line` | C + 1 | never B — a pseudo-element is not a pseudo-class |
| a combinator (` `, `>`, `+`, `~`, `\|\|`) | nothing | how to find the next compound, not what it is |
| `:not(<list>)` | the maximum over its argument list | its own name is worth nothing |

The `:not()` rule is **not** "a pseudo-class is worth one B". `:not(.a)` is
`(0,1,0)`, `:not(#a)` is `(1,0,0)`, `:not(div p)` is `(0,0,2)`, and
`:not(.a, #b)` is `(1,0,0)` — the most specific selector in its argument list,
which is the Selectors-4 rule. The argument is the parsed selector list 9.4
attached to the compound, reached through
`robo_css_compound_selector_argument_of`, so no text is ever reparsed. A `:not()`
whose argument is not reachable — a hand-built one, or a copy whose index was
cleared by `robo_css_compound_selector_append` — contributes nothing rather than
being read as a pseudo-class worth one.

A compound is the sum of its simple selectors, a complex selector is the sum of
its compounds, and a *list* has no specificity at all: each of its members has
one, and the cascade must use the one whose selector matched the element.

## The API

Four doors, one per level of the 9.1 selector model — the same shape the 9.4
matcher uses, so a caller that can match a selector can measure it with the same
records:

```c
robo_css_specificity_t robo_css_specificity_of_simple(simple, compound, simple_index);
robo_css_specificity_t robo_css_specificity_of_compound(compound);
robo_css_specificity_t robo_css_specificity_of_complex(complex);
robo_css_specificity_t robo_css_specificity_of_list_item(list, index);
robo_css_specificity_t robo_css_specificity_max_of_list(list);   /* :not()'s value */
```

All five are **total**. An absent or incoherent input yields the zero specificity
rather than failing, because "the specificity of no selector" is a real value and
a crash on a malformed record is not — so none of them returns a result code and
no caller has an error to handle.

`robo_css_specificity_of_simple` takes the owning compound and the simple
selector's index for one reason: that is how a `:not()` argument is reached.
Passing `NULL`/`0` is legal, and means "this simple selector cannot have one".

### Why there is no cache

A specificity is a pure function of the selector, so it never needs invalidating —
and that is exactly why it is not cached. A cache would give selector records a
mutable field that the parser, the compound-copy path and every hand-builder would
then have to keep correct, in exchange for saving three additions per simple
selector. Nothing profiles as hot enough to buy that. If it ever does, the cache
belongs behind these five functions, which is a change no caller can see.

## The cascade's use of it

The cascade holds **no specificity arithmetic of its own**. It calls the five
functions above, stores the result on each candidate, and compares candidates with
`robo_css_specificity_compare`:

```c
typedef struct robo_css_cascade_candidate {
    ...
    robo_css_specificity_t specificity;   /* of the selector that matched */
} robo_css_cascade_candidate_t;
```

Collection computes it once per matched rule, and resolution is then one forward
pass over an array already sorted by (level, document order):

* a strictly stronger level displaces the incumbent;
* at one level, a greater specificity displaces it;
* an **equal** specificity does too, because the array is in document order within
  a level — so the candidate being read is the later of the two, and the
  tie-break is exactly "later wins".

There is no third verdict any more. 9.5's
`ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY` described "I need specificity to
answer this", and 9.6 is that specificity.

### The selector that matched, not the list

A rule's selector list is the one place specificity could be taken from the wrong
input, so the collection walks the list itself rather than calling
`robo_css_selector_list_matches`:

> A selector list's specificity, for a match, is that of the **most specific
> selector in the list that matches the element**.

Not the first to match, and not the maximum of the whole list — a member that does
not apply must not raise the rule. So `div, #main { … }` on `<div id="main">` is
worth `(1,0,0)`, while `#absent, div { … }` on the same element is worth
`(0,0,1)`. `css.specificity.cascade.matching_selector_of_a_list` pins both, and
the second is the test that fails if the list's maximum is used instead.

The matching itself is still the 9.4 matcher's — one call per complex selector
through `robo_css_complex_selector_matches` — so nothing about *how* a selector
matches is restated. What lives in the cascade is only the list-level rule the
per-selector door does not implement.

### Inline declarations

An inline declaration carries the **zero** specificity, and that is not a mistake:
its strength is `ROBO_CSS_CASCADE_LEVEL_INLINE`, which no selector reaches. Inline
and selector declarations therefore never meet at the same level, so they never
meet in a specificity comparison either, and the two fields cannot disagree.
`css.specificity.cascade.inline_level_still_wins` is the test that says so: the
most specific selector there is (`div#x`, `(1,0,1)`) loses to a zero-specificity
inline declaration.

## Escape handling

Escaping changes which bytes an identifier resolves to, never which bucket it
belongs in. Selector parsing already interns the *resolved* spelling (9.4), so
`#item\31 23` is the id `item123` — one A — and `.foo\:bar` is one B. There is no
second escape implementation here because there is nothing to escape: specificity
is computed from records whose names are already resolved.

## Deliberately not here

| Thing | Behaviour | Why |
|---|---|---|
| `:is()`, `:where()`, `:has()` | not parsed, so not measured | the parser does not accept them; inventing specificity for a selector that cannot exist would be inventing an answer |
| `:nth-child(An+B of S)` | the pseudo-class is one B, the `of S` is not measured | the argument is not parsed as a selector list in this build |
| `@layer` | no layers exist in this model | 9.5's level table already states this |
| namespace selectors | not parsed | no namespace system exists |

`css.specificity.pseudo_classes` asserts that an *unevaluatable* pseudo-class
(`:hover`, `:lang(en)`) is still worth one B: it is a pseudo-class, and the reason
it does not match is a matching question, not a specificity one.

## Bounds and robustness

The only recursion is through `:not()`, and the argument is an owned tree, so the
depth is finite. The parser caps `:not()` nesting at
`ROBO_CSS_MAX_SELECTOR_NESTING` (8), which a parsed selector can never exceed;
`ROBO_CSS_SPECIFICITY_NESTING_MAX` (32) exists for a hand-built structure deeper
than that, and past it the walk stops descending and the deeper contribution is
zero. That is a wrong specificity for an input the parser cannot produce, which is
the right trade for a computation that must never exhaust the stack.
`css.specificity.not_chain_depth` builds chains at the bound and four times past
it and asserts both the answer and the absence of a crash.

Malformed selectors never reach a computation at all: the parser refuses them and
leaves an empty list, and asking for a specificity of an empty list answers zero.
`css.specificity.malformed_never_reaches` runs the parser's whole rejection table
through that.

## Running the suites

```bash
scripts/robo test-css              # the whole RoboCSS subsystem
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test -Filter css.specificity
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test -Filter css.dom.specificity
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test -Sanitize
```

| Suite | Prefix | Covers |
|---|---|---|
| `SuiteRoboCssSpecificity.c` | `css.specificity.*` | the tuple and its comparison, the specification's matrix, each simple selector kind, attribute operators, pseudo-classes and pseudo-elements, `:not()` including a chain past the parser's cap, compound / complex / list, combinators, escapes, saturation, a three-thousand-class stress case, the four doors agreeing, and malformed input |
| `SuiteRoboCssSpecificityCascade.c` | `css.specificity.cascade.*` | specificity deciding in the cascade, order-independence, source order after a specificity tie, level before specificity, every `!important` pairing, the matching-selector-of-a-list rule, `:not()`, a pseudo-element that matches nothing, inline, and determinism |
| `SuiteRoboCssDomSelect.c` | `css.dom.specificity.*` | the specification's `p` / `.title` / `#App .title` example over the **real** RoboHTML tree |
