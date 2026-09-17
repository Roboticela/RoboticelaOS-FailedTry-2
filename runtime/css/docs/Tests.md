# RoboCSS Test Organisation (Phase 9.1–9.6)

Twenty-nine suites, 386 tests, one framework. These phases add no test runner,
no reporter, and no assertion vocabulary: everything here goes through
`runtime/Tests/Framework` (`ROBO_CHECK*` and `robo_test_register`), the same as
every other subsystem.

The split is by **phase, then by subject**. The 9.1 suites pin the model (does a
record own what it says it owns?). The 9.2 suites pin the machine (does a byte
sequence produce the vocabulary 9.1 defined?), one suite per family of token
classes, so a failure names both the class and the rule. The 9.3 suites pin what
the grammar builds out of that stream — one per level of it (sheets and rules,
declarations, values), plus recovery and Unicode. The 9.4 suites pin the two
ends of a selector: what the grammar *makes* of a prelude (one suite, exhaustively)
and what the matcher *answers* about an element (matched, refused, or
unevaluatable — three different outcomes, so three suites against the callback
host plus one for the matcher's own structural levels). The 9.5 suites pin the
cascade in the same shape: the level table as a table (one suite, including the
two decisions it encodes), what a collection *finds and cannot use* (one suite,
which is where the counters live), and what a resolution *concludes* (one suite,
whose subject is the three keys and when one decides). The 9.6 suites pin the
third key twice over: once against the selector records (one suite, a table of
selectors and their counts, including saturation and malformed input that must
never reach a calculation) and once against the cascade (one suite, whose subject
is that specificity now decides contests the 9.5 resolver had to report).

## 1. Suite map

### 9.1 — the model

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCss.c` | 14 | Version, module init/shutdown, slot table bounds, shared types, diagnostics, capabilities, default-stylesheet boundary, the cleared-record contract |
| `SuiteRoboCssSource.c` | 11 | Source kinds, copy ownership, reconfiguration, argument rejection, labels, positions (LF / CRLF / lone CR / scalars), ranges, UTF-8 description, Unicode preservation |
| `SuiteRoboCssToken.c` | 8 | Token vocabulary, classification, bracket pairing, hash types, range binding, EOF, lifetime |
| `SuiteRoboCssValue.c` | 8 | Value kinds, tokens, function/block, nesting, move-in append, argument rejection, ownership transfer, lifetime balance |
| `SuiteRoboCssDeclaration.c` | 11 | Declaration model, value and importance, property resolution, ranges, lists, list lifetime, inline parse boundary |
| `SuiteRoboCssSelector.c` | 8 | Simple, attribute, pseudo-argument, compound, complex and list selectors, lifetime balance |
| `SuiteRoboCssSheet.c` | 12 | Stylesheet lifecycle, origin/state/name, source attachment and ownership, rule storage and nesting, the parse boundary, the parser cursor and its first-error rule |
| `SuiteRoboCssStyle.c` | 8 | Element view, its classes and attributes, the style context, borrowed callbacks, the unsupported match/compute seams, computed-style lifetime |
| `SuiteRoboCssRobust.c` | 8 | Empty, whitespace-only, large, incomplete and malformed input; repeated churn; deep rule nesting; wide structures; deep value trees; Unicode through every model |

### 9.2 — the machine

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssTokenizer.c` | 13 | Stream API and lifetime: init/dispose/reset, `next`/`peek`/`skip`/`skip_trivia`, peek sharing one slot, EOF as a value and EOF repeats, cursor state, first-error rule, the cleared-record regression, tokenizer-fits-in-a-parser |
| `SuiteRoboCssTokenizeCore.c` | 13 | Whitespace runs, comments (empty, multiline, Unicode, unterminated), CDO/CDC, punctuation, delimiters, bracket pairing, EOF range; NUL and ill-formed UTF-8 — the two ends of the byte spectrum where the no-preprocessing decision is visible |
| `SuiteRoboCssTokenizeIdent.c` | 12 | Identifiers (simple, hyphenated, underscore, `--custom`, Unicode, escaped), functions, at-keywords, hashes (`HASH_ID` vs `HASH_UNRESTRICTED`), urls (bare, quoted, escapes, malformed) |
| `SuiteRoboCssTokenizeLiteral.c` | 13 | Strings (quote forms, escapes, hex escapes, line continuations, Unicode, empty, malformed), numbers (integer, decimal, leading point, signs, exponents, extremes), percentages, dimensions and their `unit` |
| `SuiteRoboCssTokenizeSequence.c` | 12 | Whole snippets lexed end to end (`color: red;`, `margin: 10px 20px;`, the `@media` block), range/position audit, malformed inputs, streamed large-input robustness |

### 9.3 — the grammar

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssParseSheet.c` | 14 | Rule counts and source order, preludes as component values, empty blocks as real rules, at-rules (statement, block, nested, unclosed, optional `;`, never interpreted), sheet lifetimes |
| `SuiteRoboCssParseDeclaration.c` | 15 | Names and their case, value shapes, trivia around the colon and the `;`, `!important`, the omitted final semicolon, custom properties and their raw values, malformed declarations, duplicates kept in order, ranges, the inline-style boundary |
| `SuiteRoboCssParseValue.c` | 11 | The component-value vocabulary, token ranges, functions, nested functions, blocks and the close they matched, the nesting bound, strings, url tokens, value lifetimes |
| `SuiteRoboCssParseRecover.c` | 11 | The four malformed rule shapes, stray top-level braces, a broken declaration that keeps its rule, a dropped rule that costs exactly one rule, several problems in one sheet with the *first* reported, nested-rule and nested-at-rule skipping, the rule nesting bound, and the three tokenizer-integration properties |
| `SuiteRoboCssParseUnicode.c` | 6 | Preludes, string values, property names, custom properties, comments, at-rule preludes, and byte-offset ranges — over the thirteen-script `css_samples` corpus |

### 9.4 — the selectors

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssSelectParse.c` | 51 | The selector grammar: every simple selector kind, all four combinators, all six attribute comparators and the `i`/`s` flags, quoted and identifier attribute values, escapes resolved through the tokenizer (`\:`, hex escapes, leading digits), `*` in compounds, `:not()` nesting and its bound, the An+B microsyntax table (including what must be *rejected*), pseudo-elements vs pseudo-classes and the four legacy one-colon spellings, source ranges, and the malformed table |
| `SuiteRoboCssMatchSimple.c` | 13 | Type (with the ASCII fold and non-ASCII non-folding), class word semantics, id, attribute operators including empty values and case flags, and that a compound requires every one of its simple selectors |
| `SuiteRoboCssMatchCombinator.c` | 14 | The four combinators, the `>`/`+`/`~` boundaries, selector-list index and range reporting, and that a relation the host cannot answer is *undecided* rather than false |
| `SuiteRoboCssMatchStructural.c` | 7 | `:root`, `:first-child`, `:last-child`, `:only-child`, `:nth-child()`/`:nth-last-child()` incl. negatives and out-of-range forms, and `:not()` including its unevaluatable-argument path |
| `SuiteRoboCssMatchLevels.c` | 14 | The matcher's four structural doors: each driven directly, the argument contract of the lower ones, that a complex selector of one compound *is* that compound, that an incoherent record is refused by the complex door and skipped by the list door, that `>` is strictly narrower than a space, that an unavailable relation is undecided, that a cyclic host is bounded, and that the three levels agree over a wide selector table |
| `SuiteRoboCssDomSelect.c` | 17 | The same selectors against the **real** RoboHTML tree: the visual checkpoint's own document, text/comment nodes not counting as siblings, deep trees, rebuilding after mutation, determinism, matching not mutating the DOM, and that running the selectors over the checkpoint leaves the rendered picture unchanged — plus the 9.5 cascade over the same tree (`css.dom.cascade.*`): the document's own `style` attribute read through the seam, a collection that has to use real parent and sibling links to find the right rules, importance beating inline, a byte-for-byte proof that collecting a cascade changes nothing the demo paints, and (`css.dom.specificity.picks_the_winner`) a real `id`/`class`/`type` contest resolved by specificity over the checkpoint's own tree |

### 9.5 — the cascade

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssCascade.c` | 5 | The level table: every (origin, importance) pair reaching exactly one level and every level being reachable, the spelled-out rows, `ROBO_CSS_ORIGIN_NONE` and unknown origins having no level, the ascending order of all eight, the names, and the two decisions the table encodes — inline above the origin it belongs to, and an important half that is *not* a plain reversal of the normal one |
| `SuiteRoboCssCascadeCollect.c` | 15 | What applies and what could not be used: only the matching rules contribute, cascade order across origins and within one origin, `!important` moving a declaration past its normal siblings, the inline boundary (present / absent / empty / malformed / host failure), and every counter — undecided selectors, at-rule blocks, unparsed rules, destroyed sheets, origin mismatches — plus collection replacing a previous result and releasing everything it allocated |
| `SuiteRoboCssCascadeResolve.c` | 12 | The three keys and the cases that settle them: nothing declared, a single candidate, two declarations of one selector (decided by order), two selectors at one level and different specificity (decided), equal specificity (document order), a stronger level deciding *after* an undecided pair — the case a one-pass resolver gets wrong by remembering its first verdict — importance across origins and inline, identity by property id and by name, provenance, and per-property independence |

### 9.6 — specificity

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssSpecificity.c` | 20 | The three counts and their comparison: the spelled-out matrix (`*`, `div`, `.card`, `#main`, `[disabled]`, `:root`, `::before`, and the compound and complex forms), every simple-selector kind, all six attribute operators, the pseudo-class set and the pseudo-elements that must *not* count as them, `:not()` worth its argument (including nesting and an empty argument), compound and complex accumulation, combinators contributing zero, per-member selector-list values and the most-specific member, escaped identifiers, saturation at the represented maximum, a stress selector that must not wrap, agreement across the four model levels, malformed input that the parser refuses before a calculation can see it, and the API's argument contract |
| `SuiteRoboCssSpecificityCascade.c` | 10 | The key reaching the cascade: a specificity gap deciding a contest, order-independence of that decision, document order still deciding an exact tie, a stronger level outranking specificity, importance at the right stage, a rule whose selector list matched through one member, `:not()` deciding by its argument, a pseudo-element winning nothing, inline still above author, and repeated resolution producing identical results |

The parser suites are where this documentation's older claim — "they never assert
that something was *parsed*" — stops being true. From 9.3 a parse really happens,
and these suites assert node types, tag-level structure, ranges, tree shapes and
the contents of every list, exactly as the DOM suites assert the DOM.

## 2. The cleared-record contract test

`css.records.cleared_state` exists because §3 of `docs/Architecture.md` — "a
record must be cleared before its first use" — is a precondition the compiler
cannot check, and every caller of `ROBO_CSS_CLEARED` depends on the cleared state
being *exactly* what the init functions produce.

If a cleared record were ever not the same as an init'd one, every caller of
`ROBO_CSS_CLEARED` would be releasing fields nobody set. So the test asserts the
equivalence field by field, for a value, a source, a declaration, a selector, a
compound, a complex, a selector list, a rule, a rule list, an error and a parser:
the kind enumerator, the child array pointer and capacity, the absence of slices
and ranges, the derived counts (depth, tree count, child count), the names, the
error message, and the parser's source and offset.

It then goes one step further and asserts that each cleared record is a *usable*
starting point rather than an inert one: each can be configured or released
without a trip through its init function, release is idempotent, and release is
safe on a record that was cleared and never used.

This test is the reason the rule is a contract rather than a comment. It was
written *after* a real crash: a `robo_css_value_t` local reached
`robo_css_value_make_list` uninitialized, the release step walked a `count` of
stack garbage and freed a pointer that was never an allocation.

## 3. What the CSS suites do and do not assert

They assert what the **model** does with bytes — kinds, ownership, ranges,
slices, invariants, lifetimes — what the **tokenizer** does with a byte sequence
(which token type, what value, which range), what the **grammar** builds out of
that stream (which rule, at which offsets, holding which declarations and which
component-value tree), what the **matcher** answers about an element, and what the
**cascade** collects and concludes. They never assert that anything was
*computed*: specificity, inheritance and computed values remain boundaries with no
implementation behind them.

Where a boundary answers `ROBO_NOT_SUPPORTED`, the suites assert the whole shape
of the refusal, not just the code: nothing changed, the state did not advance, the
reason is recorded on the object as well as returned, the message names the goal
that will implement it, and a NULL out-parameter is tolerated.

`css.capabilities` asserts that `parser`, `source_model`, `selectors`, `cascade`,
`computed_values` and the rest report what this build *does*. A pass does not get
to round those flags up to what the phase list intends — which is why `cascade`
is asserted 1 while `inheritance` and `computed_values` are asserted 0 in the same
test, and why the comment beside it says what the 1 does *not* include.

## 4. Fixtures

`RoboCssTestSupport.h` is shared by every suite. It is not a framework; it holds
the three things a suite would otherwise copy:

* `css_fixture()` — brings RoboBase and RoboCSS up, and a `css_source_of_string`
  that builds a source from a literal without the caller tracking the length.
* `css_samples[]` — the thirteen script families every "does it preserve text"
  test walks, in one place so two tests cannot disagree about what "Arabic" means.
* Builders (`css_value_token_at`, `css_value_function_with_token`,
  `css_declaration_append`, `css_complex_selector_of`), the lexing pair
  `css_lex` / `css_lex_text` with `css_token_log_t`, `css_token_at`,
  `css_count_type`, `css_kind_label` and `css_log_kinds`, the streaming pair
  `css_stream_audit_t` / `css_audit_stream`, and the allocator measurement pair
  `css_allocator_ensure_enabled` / `css_allocator_restore`, so a lifetime test
  never leaves the debug allocator on for the next suite.

From 9.3 it also carries the parsing helpers: `css_parse_sheet` (copy the text
into a fresh sheet and parse it), `css_parse_declarations` (the inline-style
door), `css_sheet_style_rule`, `css_prelude_shape`, `css_declaration_shape`,
`css_declaration_name_of`, `css_component_is` and `css_parse_declaration_is`.

From 9.4 it carries the selector helpers (`css_select_parse`,
`css_select_parse_rejected`, `css_select_simple`, `css_select_text`) and the
element-host double (`css_host_t` with `css_host_add`, `css_host_attribute`,
`css_host_set_parent`, `css_host_seal`, `css_host_drop_relation`) — a host, not a
DOM: a tag, an id, a class list, an attribute list and three relations, which is
exactly what a view holds.

From 9.5 it carries the cascade fixtures: `css_cascade_fixture_t` with
`css_cascade_begin` / `css_cascade_end` (a context with the host double installed),
`css_cascade_sheet` (parse a stylesheet, parse every rule's selectors, add it
under an origin — the two steps a real caller performs and the cascade requires),
`css_cascade_collect`, `css_cascade_candidate_name` and
`css_cascade_winner_text`. The inline half of the seam is `css_inline_stub_t` with
`css_cascade_inline`, shared by the collection and resolution suites rather than
duplicated: one stub for one callback interface, or the two suites drift.

The host double is shared with the 9.4 suites on purpose. A cascade test that
needs to know *what* matched and a matching test that needs to know *whether*
something matched are asking one seam two questions.

The shape helpers are the ones that carry the weight. `css_shape` writes a
component-value tree as **one line** — children comma-separated, whitespace
folded to `_`, functions and blocks bracket-delimited — so
`calc(100%,_,-,_,var(--gutter,,,_,20px))` pins a whole four-level tree in one
string. A missing whitespace token, a mis-nested function or a trimmed-away leaf
all change the string, which is what makes the assertion failure-sensitive rather
than merely true.

The audit pair exists for a specific reason. The framework's expectation log is
bounded, so a test that logs one line per token fails by *overflowing the
reporter* on a several-thousand-token input — it reports nothing about the thing
it was written to check. `css_audit_stream` walks a stream counting tokens by
type and total instead, so a large-input test asserts a shape rather than a list.

Everything in the header is `static inline`. That is not decoration: a plain
`static` function in a header that one suite does not use is an unused-function
error under this project's `-Wall -Wextra -Werror`, and `static inline` is how
the framework's own headers solve the same problem. The header also casts its
UTF-8 samples carefully — `"\xC3\x9Cber"` is a single out-of-range escape
(`\x9Cb`), so the German sample splits its literal to keep the escape honest.

## 5. Running

```bash
scripts/robo test-css                     # the 386 css.* tests only
scripts/robo test-runtime                 # the whole runtime suite
scripts/robo test-css -Verbose            # per-test lines
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test
```

A focused run builds the same host binary and filters on the `css.` prefix, so a
failure in another subsystem cannot be reported against this one. The tokenizer
subtree is `--filter=css.tokenize` (63 tests); the parser subtree is
`--filter=css.parse` (59 tests); one suite is `--filter=css.tokenize.number`.

The 9.4 surface is three filters, and they do not nest the way the names suggest:
`--filter=css.select` (107) is the grammar plus the three callback-host matcher
suites **plus** the eight `css.selector.*` model tests, because `css.select` is a
prefix of `css.selector`. The precise counts are
`--filter=css.select.parse` (51), `--filter=css.select.match` (34),
`--filter=css.select.levels` (14), and `--filter=css.dom` (16) for the suites
that run against the real RoboHTML tree.

The 9.5 surface is `--filter=css.cascade` (32) for the three cascade suites —
levels, collection and resolution — and `--filter=css.dom.cascade` (4) for the
part of it that drives the real tree. `css.cascade` does not pick up
`css.dom.cascade`, because the prefix is matched against the whole name.

The 9.6 surface is `--filter=css.specificity` (30), which is both suites:
twenty value/calculation cases and ten that drive the cascade with them. The real
tree's one case is `--filter=css.dom.specificity` (1).

`--list` prints every registered name, which is how a documented test name can be
checked against the binary instead of trusted.

## 6. Sanitizers

```bash
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test -Sanitize
```

The flag probes the toolchain and enables AddressSanitizer and
UndefinedBehaviorSanitizer for the host test binary; it is documented in
`docs/Build.md`. The result for this phase:

* **AddressSanitizer + UndefinedBehaviorSanitizer: clean.** Zero sanitizer
  findings across the whole runtime suite — 1793/1793 — and `--filter=css` is
  386/386 under the sanitized binary. Phase 9.6 introduced no memory-safety or
  undefined-behaviour defect.

Run the whole suite rather than the `css.` subtree and the sanitized binary may
report **one** failure that is not a sanitizer result and not this subsystem's: `window.host.keyboard` asserts `robo_window_request_focus(id) == ROBO_OK`, and
Windows declines `SetForegroundWindow` for a process not entitled to the
foreground, so from a detached shell the call returns `ROBO_NOT_SUPPORTED` and
the case fails. It is intermittent — in the 9.3 validation runs the plain build
passed 3/3 and the sanitized one 1/2 — and it is documented in `docs/Build.md`
§6. No `window.host.*` case exercises any RoboCSS code, and no ASan or UBSan
diagnostic accompanied it.

One real defect was found by the sanitizers during 9.1 and fixed rather than
suppressed:

> `robo_css_stylesheet_parse` dereferenced its `out_error` parameter
> unconditionally when reporting the not-yet-implemented parse, even though
> `robo_css_error_set` tolerates a NULL destination and the header promises the
> record is optional. ASan reported the access at `0x0` when
> `css.sheet.parse_boundary` called it as `robo_css_stylesheet_parse(id, NULL,
> NULL)`. Fixed by building the record locally and copying it to the caller only
> when one was given — the stylesheet keeps its copy either way.

No sanitizer finding was suppressed, and no test was weakened to reach a clean
run.

Two more real defects surfaced during 9.2, both from tests rather than from the
sanitizers:

> **The hex-escape terminator rule.** `\41 b` is the ident `Ab`: the whitespace
> after a hexadecimal escape is part of the escape and must be swallowed. The
> scanner consumed it but the *value* accessor did not re-apply the same rule, so
> `robo_css_token_value_*` returned `A b` for a token whose `repr` said `Ab`.
> The accessor now applies the identical rule — the two layers are tested against
> each other so they cannot drift again.
>
> **Line continuations in strings, again across the two layers.** A backslash
> before a newline inside a string is a continuation that produces no character.
> The tokenizer classified it correctly; the value accessor wrote both the
> backslash and the newline into the resolved text.

A third finding was a **contract violation in the new suites themselves**: several
records reached `dispose`/`copy` without being `ROBO_CSS_CLEARED` first. The
first symptom was not a failed assertion but an invalid-free, which is exactly the
failure mode that contract exists to prevent — hence the regression test it
gained in `SuiteRoboCssTokenizer.c`.

`SuiteRoboCssTokenizeSequence.c` also streams its large-input audit rather than
logging every token, because the framework's expectation log is bounded and a
several-thousand-token snippet would exhaust it — a test that fails by
overflowing the reporter reports nothing useful.

## 7. What the 9.3 suites found

Three test-expectation errors and **one** class of build hazard, and no product
defect:

> **A stale object is not a source defect.** The first 9.3 run crashed inside a
> 9.1 test. `RoboCSSRule.h` had grown `prelude_values` and `block_closed`, and the
> build reused objects compiled against the old layout, so a field read landed in
> the wrong place. A clean rebuild fixed it, and the 9.1 tests that had called
> `strstr` on a now-`NULL` message were then updated to assert the real parse.
> The lesson is about the build, not the grammar: a struct layout change needs
> `-Clean`.
>
> **Three expectations were simply wrong**, and the implementation was right each
> time: `var(--gutter, 20px)` has four children and not three (the comma and the
> space are component values until something reads them as separators); `a[1]` is
> contiguous, so `x: a[1] (2) [3` is six children with the blocks at 1, 3 and 5;
> and a block's range begins at the `{`, one byte after the space that separates
> it from the prelude, not two.

A fourth was a real property of the corpus rather than of the parser: four of the
thirteen `css_samples` entries are two words, and a property name is one
identifier, so the property-name test uses each sample's first word and its
value test uses the whole sample. Asserting otherwise would have been asserting
that a space is an identifier character.

## 8. What the 9.4 suites found

One product defect, caught by UBSan; one gap in the matcher's *public surface*,
caught by asking which functions anything actually calls; and six test
expectations that were wrong — four of them wrong about the fixture, two wrong
about what CSS itself says.

> **Signed overflow in the An+B digits.** A long enough run of digits in
> `:nth-child(...)` overflowed the accumulator — `long` is 32-bit on Windows —
> and UBSan reported it as `signed integer overflow`. The fix bounds the
> accumulation at `ANB_VALUE_MAX` inside `anb_digits` and *rejects* the form that
> would exceed it, and the bound test is written as a division
> (`value > (MAX - digit) / 10`) so the check itself cannot overflow. The suite
> asserts the oversize argument is refused, rather than wrapped into a plausible
> wrong sequence.

> **Three matcher entry points nobody called.** `robo_css_simple_selector_matches`,
> `robo_css_compound_selector_matches` and `robo_css_complex_selector_matches` are
> declared in `RoboCSSMatcher.h` and documented as independently testable, and
> every existing matcher test reached the levels through the *list* door. All
> three could have been broken, or disagreed with the list, and the whole suite
> would have stayed green. `SuiteRoboCssMatchLevels.c` drives each door directly
> and then requires the levels to agree over a 24-selector × 7-element table —
> the cross-level check is the one that would catch a change to the shared walk
> that moved two doors in different directions.

> **Four expectations were wrong about the fixture.** Two hosts had elements added
> without a parent and then asserted `h1 + p` and `:nth-child(2)`; the host double
> derives siblings from the parent link, and two elements with no parent are not
> siblings (this is the `css_host_seal` rule, and it exists precisely so two roots
> are not treated as siblings). Giving the elements a parent fixed the fixture
> rather than the code. A third passed `NULL` for the matcher's output and
> expected a verdict, where the output is required. The fourth asserted that a
> cyclic host leaves a `>` selector undecided, when a `>` chain recurses once per
> compound and so terminates on its own — the *descendant* walk is the one that
> needs the step limit, and the suite now pins both behaviors side by side.

> **Two expectations were wrong about CSS.** `[data-x=1]` is not a valid
> attribute selector: an unquoted value must be an identifier, and a bare number
> is not one, so the parser is right to reject it (`[data-x="1"]` is the spelling
> that works). And `div + , span` cannot be parsed as "a list with one broken
> member": a selector list is all-or-nothing by design, so the parser rejects the
> whole thing. The skip-an-incoherent-member guard therefore only ever sees
> hand-built input, and the suite now builds it that way — asserting, in the same
> test, that the parser refuses the text that would have produced it.

## 9. What the 9.5 suites found

One build-integration defect that had been hiding behind a green 9.4 claim, one
wrong row in a test's picture of CSS, and two mistakes in the *fixtures* that the
implementation was right about.

> **The DOM suite could not be built.** `SuiteRoboCssDomSelect.c` lives in
> `Tests/Suites/` — it is the one suite that must see RoboCSS and RoboHTML
> together, and the two are peers whose guards forbid each other's headers, so it
> belongs in neither directory — but it includes `css/tests`'
> `RoboCssTestSupport.h`, and the host compile line never had that directory on its
> include path. A suite in its own test directory resolves the header relatively;
> a suite outside one needs `-I`. The 9.4 record reported the whole suite green,
> so this is the first thing the 9.5 work had to fix, and the reason the counts in
> this document are worth checking against the binary (`--list`) rather than
> trusted.

> **The fixture was an id where the rules wanted a class.** Three resolution tests
> asserted that `div` and `.app` could not be separated without specificity while
> the host was built with `css_host_add(host, "div", "app", NULL)` — that is the
> *id* `app`, so `.app` matched nothing, one candidate arrived, and the cascade
> answered `DECIDED` where the test expected `UNDECIDED_SPECIFICITY`. The
> implementation was right: a single candidate is decided however the winning
> selector was spelled. The lesson is the one the 9.4 record already learned — a
> fixture that disagrees with its own rules produces a test that asserts the wrong
> thing about real code — and it is why every cascade test now adds its elements
> *before* the sheet that selects them.

> **One expectation was wrong about CSS itself.** `css.cascade.levels.inline_is_author_level`
> first asserted that an inline declaration loses to a *user* rule. It does not:
> author-origin normal declarations beat user-origin ones, and an inline `style`
> attribute is an author declaration with a specificity no selector reaches. The
> table was right and the expectation was wrong, which is exactly the class of
> mistake the spelled-out assertions beside the table exist to catch — a level
> table that merely returns *a* level for every input would have passed the wrong
> test.

> **A counting sort is where a candidate can be lost.** `cascade_order_by_level`
> counts and then places over the same bucket function, deliberately, because a
> candidate counted into one bucket and placed from another is the one failure a
> counting sort has. It is invisible to every other test: the array would simply
> be one element shorter than the collection's own count.

## 10. What the 9.6 suites found

No product defect and no sanitizer finding. What the phase did find was a public
API change that had to be made rather than tested, and one expectation written
before the implementation that CSS disagrees with.

> **The third verdict had to be deleted, and that is the riskiest thing here.**
> 9.5 shipped `ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY` and its name, and
> the 9.5 tests asserted both. 9.6 makes the cascade total, so the verdict
describes nothing and is gone: `ROBO_CSS_CASCADE_VERDICT_COUNT` is 2, a value that
> used to name the third verdict is no longer known (`verdict_is_known` returns 0
> for it), and its name string is gone with it. Removing an enumerator from a
> public header breaks a caller that switches on it, which is why the API is not
> frozen. The alternative — keep the dead verdict so nothing has to change — would
> leave a documented outcome that can never occur, the same failure mode the
> "no flag claims a half-built feature" rule exists to prevent.

> **`:not(:not(.a)):not(#x)` is (1,1,0), not (1,0,0).** The matrix was written
> before the code, and this row was copied from the simpler
> `:not(:not(#x))` = (1,0,0) without thinking the two `:not()`s through
> separately: the outer one is worth *its argument's whole specificity*, so
> `:not(:not(.a))` is (0,1,0) and the `:not(#x)` beside it adds the (1,0,0). The
> implementation was right and the expectation was wrong. The neighbouring rows
> (`:not(#x)` and `:not(:not(#x))`, both (1,0,0)) exist precisely so that the
> nesting is pinned as "the argument's specificity, however deep" rather than by
> the one case that would pass under either reading.

> **Two tests had to say something different, not something weaker.**
> `css.cascade.resolve.same_level_rivals_are_*` and
> `css.cascade.collect.two_sheets_in_one_origin` both asserted the 9.5 undecided
> answer. They were rewritten to assert the *decision* and, in the same test, that
> the decision is about specificity rather than order — the rival selector is the
> one written *later* in both cases, so a resolver that had simply fallen back to
> document order would have picked the same winner the specificity key picks, and
> the test would have proved nothing. The collect case goes further and asserts
> `robo_css_specificity_equal` on the two candidates before asserting the winner,
> so "this was decided by order" is a fact the test establishes rather than an
> assumption it leans on.
