# RoboCSS — selectors (9.4)

Two halves that meet at one data structure. The **selector grammar** turns a
style rule's prelude into a selector list; the **matcher** answers whether one
element satisfies one selector. Neither knows about the other, and neither knows
about the DOM.

```text
a rule's prelude (component values, 9.3)
    ↓  robo_css_selector_list_parse_*     (9.4, this document)
robo_css_selector_list_t                    simple / compound / complex / list
    ↓  robo_css_*_selector_matches         (9.4, this document)
a verdict, and whether it was a verdict
```

There is **no document, no style resolution and no cascade** here. The matcher
asks a *host* (`RoboCSSStyle.h`) about one element at a time; the DOM integration
suite is what proves the same answers hold against the real RoboHTML tree.

`robo_css_get_capabilities` reports `selector_matching = 1`. The cascade,
specificity, inheritance and computed values remain 0 and are not stubbed.

---

## 1. The two doors

| Entry point | Input | Output |
|---|---|---|
| `robo_css_selector_list_parse_text` | a caller-owned NUL-terminated string | a selector list |
| `robo_css_selector_list_parse_slice` | @p length bytes | a selector list |
| `robo_css_selector_list_parse_range` | a range of a `robo_css_source_t` | a selector list |
| `robo_css_style_rule_parse_selectors` | a parsed style rule | **fills the rule's `selectors`** |
| `robo_css_an_plus_b_parse` | An+B text | an `{a, b}` form |
| `robo_css_an_plus_b_matches` | a form and a 1-based index | in-sequence or not |

`robo_css_style_rule_parse_selectors` is the call that closes 9.3's open
question — 9.3 deliberately left every rule's `selectors` empty and kept the
prelude as component values, so that "has this prelude been understood as
selectors yet" would be answerable by asking. It is deliberately the **only** way
to fill `selectors`: a rule's selectors are whatever its prelude describes, and
there is no second path that could disagree. It replaces on success (the previous
list is disposed) and so is idempotent, and on failure the rule keeps an empty
list and is otherwise untouched — a rule whose prelude is not a selector list is
still a rule, which is the 9.3 decision that malformed CSS produces a model
rather than a failed parse.

The three `parse_*` doors differ only in what the bytes are and what the ranges
are measured against: `parse_text` makes the text its own coordinate system
(ranges start at 0), `parse_range` produces **source** offsets, which is what a
stylesheet's own diagnostics want.

---

## 2. It lexes nothing

Every byte arrives as a 9.2 token, and every name the grammar records is that
token's **value** — the tokenizer's own escape rule, already applied. So

```css
.foo\:bar { }
```

records the six bytes `foo:bar`, `#item\31 23` records `item23`, and a matcher
never has to know that escapes exist because it compares resolved bytes on both
sides. This is also why there is no second escape implementation: 9.2 has one and
9.4 uses it.

### The name arena

A parsed selector's `name`, `attribute` and `attribute_value` slices point into
the **list's own arena**, not into the source and not into the caller's string.
Two consequences, both load-bearing:

- They stay valid for the life of the list, and they stay valid **across
  appends** — the parser reserves the arena up front from the prelude's length,
  so interning can never reallocate a name it already handed out.
- The caller's text does **not** have to outlive the list. It has to be readable
  for the duration of the parse call.

`:not()`'s argument is a *nested selector list* owned by the compound that holds
the simple selector, and the simple selector addresses it by **index**
(`nested_index`), not by pointer. That keeps a simple selector owning nothing,
which is what makes `robo_css_compound_selector_append` a safe copy.

---

## 3. Errors

A selector list is **all-or-nothing**, as CSS says it is — and that is the rule
that makes `:is()` worth having later. So one bad selector in a list makes the
whole list invalid:

```c
robo_css_selector_list_t list = ROBO_CSS_CLEARED;
robo_css_error_t error;
robo_css_error_init(&error);

/* `div, > p` — the second selector has no compound before its combinator. */
robo_css_selector_list_parse_text(&list, "div, > p", &error, NULL);
/* → ROBO_PARSE_ERROR, list is empty, error is a real position and a real message */
```

`out_error` is always written, on success too (kind `NONE`), so a caller that
never checks a return code still cannot be handed a record describing a previous
call. `out` is *initialised first*, following the 9.3 grammar's rule, so a caller
may hand over an unused or cleared list.

Nothing here fails the process, allocates unboundedly, or recurses unboundedly.
`:not()` nesting is capped by `ROBO_CSS_MAX_SELECTOR_NESTING` (8), and past the
cap the selector is **rejected** rather than parsed more deeply — the depth costs
stack, so it is a budget rather than a suggestion.

---

## 4. The matcher

Four entry points, one per structural level, and each is a pure function of
(selector, host, element):

| Entry point | Asks |
|---|---|
| `robo_css_simple_selector_matches` | does one simple selector hold of this element? |
| `robo_css_compound_selector_matches` | do **all** of this compound's simple selectors hold of the same element? |
| `robo_css_complex_selector_matches` | does this selector hold, combinators included? |
| `robo_css_selector_list_matches` | does **any** selector in this list hold? |

They compose: a complex selector of one compound *is* that compound, and a
one-selector list *is* that selector. `SuiteRoboCssMatchLevels.c`
(`css.select.levels.*`) walks a table of (selector, element) pairs and requires
the three lower doors to agree, so a change that moved two of them in different
directions fails there and nowhere else.

### The result

| Field | Meaning |
|---|---|
| `matched` | 1 when the selector matched |
| `unsupported` | 1 when some condition **could not be evaluated**, so `matched == 0` means "not decided" rather than "no" |
| `condition` | the pseudo-class name that stopped it — **borrowed from the list's arena**, so read it before disposing |
| `selector_index` | which complex selector of the list matched |
| `range` | that selector's extent, when one matched |

`matched` and `unsupported` are independent and both can be set: a list whose
first selector is `:hover` and whose second is `.card` *does* match a card, and
one member of it could not be evaluated. A caller that reads only `matched` gets
the answer it needs; a caller that also reads `unsupported` learns not to trust a
negative.

Return value: `1` matched, `0` not matched, `-1` for a caller bug (a NULL
selector, a NULL or unusable host) or a structurally incoherent record.

### How a complex selector is evaluated

Right-most compound first — the one the candidate element must be — then
leftwards through the combinators, **with backtracking**. A greedy walk is
wrong: `a b c` must be able to reject an ancestor `b` and try a higher one, and
`.x .y .z` can match through either of two `.y`s.

The recursion is bounded by the number of **compounds in the selector**, never by
the tree, so the stack cost is a property of the stylesheet rather than of the
document. A `>` chain recurses once per compound; a space chain walks. A host
whose parent links form a cycle is not a document, and the walk stops after
`MATCH_WALK_LIMIT` steps with `unsupported = 1` rather than a wrong answer or a
hang.

### Two facts about a relation

A host may supply a view and nothing else, and such a host must not be mistaken
for a document:

| | Meaning |
|---|---|
| the callback is absent | a condition this build **cannot evaluate** → `unsupported` |
| the callback answered `NONE` | a **fact**: there is no such element |

The second is why `:first-child` *is* "no previous sibling", and the first is why
a host with no parent callback cannot answer `:root` — a `"tag == html"` shortcut
would report `:root` as matched on every element of such a host. Every caller
that would read an answer as a fact goes through `match_relation_answer`, which
refuses the answer when the relation was unavailable.

### Short-circuiting

A compound is evaluated in written order and stops at the first failure, so a
cheap type test written before an expensive `:nth-child()` is genuinely cheaper.
This is observable: against a `div`, `p:hover` fails on the type test and is
**decided** false (`unsupported == 0`), while `div:hover` reaches `:hover` and is
**undecided** (`unsupported == 1`). Same pseudo-class, same element, opposite
epistemic state.

---

## 5. What is supported

**Simple selectors.** Type, universal, class, id, and attribute with all six
comparators plus existence:

| Form | Meaning |
|---|---|
| `[a]` | the attribute is present |
| `[a=v]` | exact |
| `[a~=v]` | whitespace-separated word |
| `[a\|=v]` | `v` or `v-…` |
| `[a^=v]` `[a$=v]` `[a*=v]` | prefix / suffix / substring |

Values may be quoted or an identifier — `[a=abc]` is legal, `[a=1]` is **not**
(a bare number is not an identifier) and is rejected, as it is in CSS. The
`i` and `s` case flags are parsed and recorded.

**Combinators.** Descendant, child, next sibling, subsequent sibling.

**Pseudo-classes.** `:root`, `:first-child`, `:last-child`, `:only-child`,
`:nth-child()`, `:nth-last-child()` and `:not()`. Sibling counting is **element**
counting: the host is asked for the previous and next element sibling, so a text
node or a comment between two elements neither hides a sibling nor counts as one.

**An+B.** `odd`, `even`, an integer, `n`, `-n`, `+n`, and an `n` form with a
signed offset (`2n+1`, `-3n-2`, `n-1`, `2n + 1`), with whitespace allowed around
the sign and around the whole expression and nowhere else — so `2n+`, `n*n` and
`2 n` are rejected rather than guessed at.

**Pseudo-elements.** `::before`, `::after`, `::first-line`, `::first-letter` are
parsed and **distinguished from pseudo-classes**, including the four legacy
one-colon spellings (`:before` etc.), which every engine still accepts. They are
represented, not rendered.

---

## 6. What is deliberately not

| Thing | Behavior | Why |
|---|---|---|
| `:hover`, `:focus`, `:active`, `:lang()`, `:nth-of-type()`, … | parses, matches nothing, `unsupported = 1`, `condition` names it | needs input state or a subject this build does not have; reported rather than assumed |
| `::before`, `::after`, … | **decided false**, not unsupported | the question *was* answered: this element is the `div`, not the content before it |
| the `\|\|` column combinator | parses, represented, `unsupported = 1` | needs a table model the DOM does not have |
| applying a style | collected, not applied | 9.5's cascade gathers what matches an element and orders it (`docs/Cascade.md`); turning it into a value is 9.8 |
| specificity | not computed | 9.6 — and because it is missing, a contest between two declarations of one cascade level is *reported* as undecided rather than settled by document order |

There is no selector index, no bloom filter and no per-document cache. The
matching API is shaped so those can be added behind it — the matcher takes a host
and one element, so an index is a second host, not a rewrite.

---

## 7. The host seam

`RoboCSSStyle.h` defines `robo_css_element_host_t`: one `userdata` and four
callbacks.

| Callback | Answers |
|---|---|
| `view` | describe this element: tag, id, classes, attributes |
| `parent` | the parent **element**, or `NONE` |
| `previous_sibling` | the previous **element** sibling, or `NONE` |
| `next_sibling` | the next **element** sibling, or `NONE` |

One `userdata` for all four, because they come from one host — a host is a
document, a document is one object, and four contexts to keep in step would be
four chances to get it wrong. `view` is the only required member, which is
exactly what makes "a host with only a view" a real configuration rather than a
broken one.

The host returns an **opaque ref** that RoboCSS never dereferences and never
compares except for equality. That is what keeps RoboCSS a peer of RoboHTML
rather than a client of it: nothing in `Source/RoboCSS/` includes a RoboHTML
header, and the DOM integration suite supplies the callbacks that make the real
tree visible one element at a time.

---

## 8. Lifetime

| Object | Owner | Released by |
|---|---|---|
| selector list | whoever asked for it | `robo_css_selector_list_dispose` |
| complex selectors, compounds, combinators | the list / the complex selector | the same dispose, transitively |
| nested `:not()` argument lists | the compound that holds the simple selector | the same dispose, transitively |
| the name arena | the list | the same dispose |
| simple selectors | nothing — they own nothing and are safe to copy | — |

Dispose is idempotent and safe on NULL, and a record consumed by a move
(`robo_css_complex_selector_append_compound`, `robo_css_selector_list_append`) is
left empty so disposing it again is a no-op rather than a double free.

`robo_css_selector_list_append` **moves**; `robo_css_compound_selector_append`
**copies**, because a simple selector owns nothing. The copy's `nested_index` is
cleared: an index names a list inside the compound that issued it, so carrying
one into a different compound could only ever be wrong.

---

## 9. Running the tests

```sh
# the whole selector surface — parsing, matching, levels, DOM integration
./Out/RoboTest.host.exe --filter css.select

# one group
./Out/RoboTest.host.exe --filter css.select.parse      # 51
./Out/RoboTest.host.exe --filter css.select.match      # 34
./Out/RoboTest.host.exe --filter css.select.levels     # 14

# a single test
./Out/RoboTest.host.exe --filter css.select.match.child
```

| Suite | Covers |
|---|---|
| `SuiteRoboCssSelectParse.c` | the grammar: every simple selector, every combinator, every attribute operator, escapes, `:not()` nesting, An+B, the malformed table |
| `SuiteRoboCssMatchSimple.c` | type, universal, class, id, attribute operators, case flags |
| `SuiteRoboCssMatchCombinator.c` | the four combinators, selector lists, unavailable relations |
| `SuiteRoboCssMatchStructural.c` | the structural pseudo-classes and `:not()` |
| `SuiteRoboCssMatchLevels.c` | the four matcher doors, and that they agree |
| `SuiteRoboCssDomSelect.c` | the same selectors against the **real** RoboHTML tree, including the visual checkpoint document |

`SuiteRoboCssDomSelect.c` is what ties the verdict to the runtime: it builds the
visual checkpoint's DOM out of the same source the renderer uses, runs the
selectors over it through the real host, and asserts the picture is unchanged —
which is the 9.4 half of "the visual vertical slice must not break".

See `Tests.md` for the full map, the fixture rule and the sanitizer command.
