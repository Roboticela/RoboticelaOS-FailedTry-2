# RoboCSS — the parser (9.3)

The grammar that turns a token stream into RoboCSS's syntax model. It is the
CSS Syntax *structure* rules and nothing more: it builds rules, at-rules,
declaration blocks and component values, and it interprets none of them.

```text
source bytes
    ↓  robo_css_tokenizer_*          (9.2)
token stream
    ↓  grammar_consume_*             (9.3, this document)
robo_css_stylesheet_t / robo_css_declaration_list_t
```

Selector matching, the cascade, specificity, inheritance and computed values are
**not** here and are not stubbed. `robo_css_get_capabilities` reports `parser = 1`
and `selector_matching` / `cascade` / `inheritance` / `computed_values` = 0.

---

## 1. The two entry points

| Entry point | Input | Output |
|---|---|---|
| `robo_css_stylesheet_parse` | the stylesheet's attached source | the stylesheet's rule list |
| `robo_css_declaration_list_parse_text` | a caller-owned byte buffer | a declaration list |

They are two doors into **one** grammar. Both call
`grammar_consume_declaration_block` for a block's contents, so a `style`
attribute and the inside of a `{ … }` cannot drift apart — and the inline-style
boundary (9.1.10) needed no second implementation to be filled.

Both are **transactional**. `robo_css_stylesheet_parse` builds into its own rule
list and only a parse that returned `ROBO_OK` replaces the stylesheet's rules,
so parsing twice is idempotent rather than additive and a failed attempt leaves
the previous rules exactly as they were. `robo_css_declaration_list_parse_text`
fills a fresh list; the caller disposes it either way.

Both distinguish **the call's outcome** from **the CSS's problems**:

| | Where it lives | What it means |
|---|---|---|
| The call | the return value | `ROBO_OK` whenever a model was built, *including* one built by recovering; `ROBO_NOT_FOUND` for an unresolvable id; `ROBO_OUT_OF_MEMORY` when nothing could be built |
| The CSS's problems | `out_error` | the **first** problem recovered, or a record of kind `NONE` when the input was clean |

`out_error` is always written, never left untouched. The first problem wins and
later ones do not overwrite it, because the first is the one that explains the
rest. The same record is kept in the stylesheet's last-error slot, so a caller
that did not ask for it still sees why. The state becomes `PARSED` on any
completed attempt *including* a recovered one — `PARSED` means "there are rules
here", not "nothing was wrong".

---

## 2. What the grammar is

`Source/RoboCSS/RoboCSSParserGrammar.c`, one translation unit, in the order the
specification presents the algorithms:

| Stage | Produces |
|---|---|
| `grammar_consume_rule_list` | a stylesheet, or a group block's contents; `top_level` is what makes `<!--` / `-->` ignorable |
| `grammar_consume_rule` | one rule — an at-rule when an at-keyword is next, otherwise a qualified rule |
| `grammar_consume_at_rule` | `@name prelude ;` or `@name prelude { … }`, with the block's rules **or** its descriptors |
| `grammar_consume_style_rule` | a prelude (component values) plus a declaration block |
| `grammar_consume_declaration_block` | a declaration list |
| `grammar_consume_declaration` | one declaration, or a kept-and-marked-invalid one |
| `grammar_consume_component_values` / `_component_value` | the value tree |
| `grammar_consume_block` / `_function` / `_container` | the recursion: `( )`, `[ ]`, `{ }` and `name( … )` |

Three decisions shape it:

**The grammar lexes nothing.** Every byte it sees arrives as a `robo_css_token_t`
from 9.2, and every token it keeps is the *same record* 9.2 produced — same type,
same range, same hash classification, same dimension unit, same numeric payload.
There is no path from the parser to the source bytes except through a token, and
the integration suite proves it by re-lexing the source independently and
requiring every leaf of the parse to match a token at the same range with the
same classification.

**A prelude is component values, not a selector.** `p, .a > b { … }` produces a
value list holding `p`, `,`, whitespace, `.`, `a`, whitespace, `>`, whitespace,
`b`. Nothing was matched, nothing was interpreted, and which of those is a
combinator is 9.4's question. A style rule's `selectors` collection stays empty.

**An at-rule is syntax and no more.** `@media` and `@nonsense` go through exactly
the same code. No name is looked up in a table (there is no table), nothing is
fetched, and no `@media` is evaluated. The one thing the code *can* notice
without a table is what a block contains, because that is a property of the bytes
rather than of a name — see §5.

---

## 3. Nesting, and the one recursion

The value path recurses once, in `grammar_consume_component_value`, and the rule
path recurses once, from a group block into `grammar_consume_rule_list`. Both are
bounded by `ROBO_CSS_MAX_NESTING` (64).

Past the bound the construct is **flattened, not abandoned**: the tokens are still
consumed so the parse resumes in the right place, the rule or at-rule is still
*produced*, and its contents are dropped rather than recursed into. The problem is
recorded as `RECOVERABLE_SYNTAX`. A deep construct therefore costs bounded stack
and loses only what is inside it — never the rules after it.

---

## 4. Declarations, `!important`, and what is kept

| Input | Result |
|---|---|
| `color: red` | `PARSED`, name `color`, value `red` |
| `color: red !important` | `PARSED`, **`important = 1`**, and the flag is *removed* from the value — finding it is what removes it, so no value carries a stray `!important` |
| `color:red` | `PARSED`; whitespace around the colon is not required |
| `color: red` (last in block, no `;`) | `PARSED`; the `}` terminates it |
| `color` | `INVALID`, name kept, **no value invented**, the reason recorded |
| `: red` | `INVALID`, nameless, ranges are the empty range at the colon |
| `color:` | `PARSED` with an empty value — an empty value is a legal value |
| `broken;` | `INVALID`; the entry was declaration-shaped, so it is kept rather than silently dropped |
| `--main-color: #06c` | `PARSED`; a custom property's value is kept exactly as written, including things that are not values anywhere else (`--x: { a: b }`, `--y: a, b`) |

A value's **surrounding** whitespace is trimmed, because `color:  red  ` and
`color:red` are the same declaration. Its **interior** whitespace is a component
value like any other, so `margin: 10px 20px` is five children — two dimensions
and the space between them — and a later phase can tell the two dimensions apart
by the whitespace rather than by re-reading bytes.

Both a declaration and a block keep the closing `}` out of the value's extent, so
`value_range.end` is where the author stopped writing the value.

---

## 5. At-rules: rules *or* descriptors

A block's contents are rules for some at-rules and declarations for others —
`@media` and `@keyframes` contain many things, `@font-face` and `@page` describe
one — and an at-rule this build has never heard of could be either.

So the block is read as a **rule list first**, because a rule carries more
information (a nested prelude *and* a nested block, not just a name and a value).
When that yields nothing, the same bytes are read again as a declaration list. The
second reading runs on a **tokenizer of its own over the same source**, so every
offset it reports is still an offset into the caller's bytes and no range has to
be adjusted afterwards.

The guess can be wrong: `@font-face { font-family: X }` read as a rule list
reports a malformed rule for input that is perfectly legal. That is why the
grammar saves its error state around the guess and restores it when the second
reading succeeds — the first reading's complaint described *this build's guess*
rather than the stylesheet, so it is discarded and the successful reading speaks.

---

## 6. Recovery

Recovery is the normal case, not the exceptional one. It has exactly two
boundaries, and both are the specification's:

**`grammar_skip_declaration_remnants`** — everything up to the `;` that ends a bad
declaration, or a balanced `{ … }`, or the `}` that closes the enclosing block.
The enclosing `}` is deliberately **not** consumed: it belongs to the block the
caller is filling, and eating it would end that block one declaration early, which
is how a recovery path turns one malformed declaration into a lost rule.

**`grammar_skip_braced_block`** — a `{ … }` consumed with nothing built from it,
counting only braces. This is the over-depth path.

What each problem costs:

| Construct | What happens |
|---|---|
| `}` at the top level | recorded, consumed, parse continues — a stray brace cannot make the loop read it forever |
| `{ color: red }` (empty prelude) | the rule cannot exist, so it is dropped and its block consumed; `MALFORMED_RULE` |
| `p color: red` (no block) | prelude consumed to EOF, rule dropped; `UNEXPECTED_EOF` |
| `p { color: red;` (unclosed block) | the rule **is** produced, with `block_closed = 0`; the prelude and the declarations it had read are intact |
| rule inside a style rule's block | CSS Nesting, a later phase: consumed whole and skipped, `RECOVERABLE_SYNTAX`. Balancing is what stops the skip from ending the parent early |
| at-rule inside a style rule's block | the same treatment, named for what it is |
| nesting past the bound | contents dropped, rule still produced; `RECOVERABLE_SYNTAX` |

Nothing is discarded that the model has a place for. A rejected declaration is a
declaration with `state = INVALID` and `is_usable() == 0`, so the count reflects
what the author wrote and not what the parser liked.

---

## 7. Source ranges

Every rule, at-rule, declaration and value carries a half-open byte range, and
`robo_css_source_range_is_valid` answers whether one lies inside a source. The
conventions:

- a rule's range spans its prelude through its closing `}` (or its `;` for a
  statement at-rule);
- a prelude's range excludes the whitespace that separates it from `{`, and a
  block's range begins **at the `{`**;
- a declaration's range spans its name through the end of its value, excluding
  the `;`;
- a value's range is the bytes it covers, so `range` and the tokens can never
  disagree;
- past the nesting bound a range may span the flattened contents, and the
  recorded problem says so.

Every range a parse reports is inside the source — asserted for a fixture that
contains a custom property, a `calc()`, an attribute selector and an at-rule.

---

## 8. Lifetime and ownership

Same conventions as the rest of RoboCSS, and no new ones:

- the grammar is a **stack record** (`robo_css_grammar_t`) holding a tokenizer by
  value; the only parsing-time allocation is the `@font-face`-style second
  reading's own tokenizer, which is a stack record too;
- everything the grammar builds is **moved** into its destination
  (`robo_css_value_append`, `robo_css_rule_list_append`,
  `robo_css_declaration_list_append`), so a parse leaks nothing on the error path;
- slices and tokens **borrow the source** and are never freed by RoboCSS. A
  stylesheet that owns its source keeps every slice it produced valid for as long
  as the sheet lives;
- `robo_css_declaration_list_parse_text` borrows the caller's buffer, so **the
  text must outlive the list**. That is the documented inline-style contract and
  it did not change when the parse became real;
- the OR-return guard at the bottom of each loop (`if (grammar_offset(g) ==
  before) return`) is a belt on top of every branch's bra: a loop that stopped
  making progress would spin forever, and no future edit may introduce one.

---

## 9. Tests

| Suite | Tests | What it pins |
|---|---|---|
| `SuiteRoboCssParseSheet.c` | 14 | rule counts and order, at-rules (statement, block, nested, unclosed, optional `;`), empty and comment-only sheets, lifetimes |
| `SuiteRoboCssParseDeclaration.c` | 15 | names and case, value shapes, trivia, `!important`, semicolons, custom properties and their raw values, malformed declarations, duplicates, ranges, the inline-style boundary |
| `SuiteRoboCssParseValue.c` | 11 | the component-value vocabulary, token ranges, functions, nested functions, blocks and their matching close, the nesting bound, strings, url tokens |
| `SuiteRoboCssParseRecover.c` | 11 | malformed rule shapes, stray braces, kept-broken-declarations, dropped rules that cost one rule, several problems in one sheet, nested-rule and nested-at-rule skipping, the rule nesting bound, and the three tokenizer-integration properties |
| `SuiteRoboCssParseUnicode.c` | 6 | preludes, string values, property names, custom properties, comments, at-rule preludes and byte-offset ranges, over the thirteen-script `css_samples` corpus |

Fifty-seven cases, plus `css.parser.cursor` and `css.parser.first_error` from 9.1 —
**59** under `css.parse`, out of 208 in the subsystem.

```bash
scripts/robo test-css                            # the whole RoboCSS subsystem
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Test -Filter css.parse
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Sanitize -BuildTests
./runtime/Out/RoboTest.host.exe --filter css.parse
```

`-Filter` is a prefix match on the registered test name, so `css.parse` selects
the parser and nothing else. The three `css.parse.integration.*` cases are the
ones to keep green when the grammar changes: they are what makes "the parser
consumes the tokenizer" a tested property rather than a claim about which headers
are included.

---

## 10. What this phase does not do

- No selector parsing, matching or specificity. A prelude is component values.
- No cascade, no inheritance, no computed values. `!important` is *recorded*, and
  nothing is ordered by it.
- No property semantics. A property name is a slice; `robo_css_declaration_resolve_property`
  is the hook 9.8 will use, and nothing calls it from the parse.
- No at-rule semantics: no `@media` evaluation, no `@import`, no `@font-face`
  loading, no `@keyframes` interpretation.
- No stylesheet loading, no networking, no CSS Nesting, no CSSOM.
- No layout, painting or rendering, and no Chromium code.
