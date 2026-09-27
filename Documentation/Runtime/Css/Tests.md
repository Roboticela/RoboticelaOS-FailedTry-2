# RoboCSS Test Organisation (Phase 9.1–9.16)

Forty-two suites, 609 tests, one framework. These phases add no test runner,
no reporter, and no assertion vocabulary: everything here goes through
`runtime/Tests/Framework` (`RoboCheck*` and `TestRegister`), the same as
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
is that specificity now decides contests the 9.5 resolver had to report). The
9.8 suites pin the typed value twice over: once as a model (one suite: the
closed set of shapes, the unit table, the colour forms, the normaliser's seven
computations and the argument contract) and once as a pipeline (one suite: the
chain door against the real RoboHTML DOM, where the subject is what a child
actually ends up with). The 9.9 suite pins the colour in three groups, and the
groups are the argument: the **record** (kinds, predicates, kind-aware equality,
the outcome vocabulary, serialization), the **syntaxes** (the named table walked
whole, all four hex lengths, both argument styles of both functions, the clamping
and the hue normalisation, the deferred spaces, and the malformed shapes), and the
**integration** (colours through the value layer and the computed style, including
the two `currentColor` rules and the `var()` boundary). The syntaxes are driven
through *declaration text* rather than hand-built component values wherever a
declaration can say the same thing, because that is the path a stylesheet takes —
a hand-built tree would prove the parser works on trees the real parser never
produces, which is the half of the job that cannot break.

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
| `SuiteRoboCssDomSelect.c` | 35 | The same selectors against the **real** RoboHTML tree: the visual checkpoint's own document, text/comment nodes not counting as siblings, deep trees, rebuilding after mutation, determinism, matching not mutating the DOM, and that running the selectors over the checkpoint leaves the rendered picture unchanged — plus the 9.5 cascade over the same tree (`css.dom.cascade.*`): the document's own `style` attribute read through the seam, a collection that has to use real parent and sibling links to find the right rules, importance beating inline, a byte-for-byte proof that collecting a cascade changes nothing the demo paints, (`css.dom.specificity.picks_the_winner`) a real `id`/`class`/`type` contest resolved by specificity over the checkpoint's own tree, and (`css.dom.computed.*`, 9.8) the whole pipeline ending in a computed style over the same document. The **Phase 18 integration** (18.21, sixteen cases) closes the same seam for the families Phase 18 added: `css.dom.{flex,grid,table,column,transform,transition,animation,container,logical,writing,font,text,image,blend,border.decoration,filter}.pipeline`, each carrying a stylesheet selector through the real DOM host adapter, the cascade, inheritance and computation to a typed value — a family that only worked against the CSS-side host double fails here |

### 9.5 — the cascade

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssCascade.c` | 5 | The level table: every (origin, importance) pair reaching exactly one level and every level being reachable, the spelled-out rows, `RoboCssOriginNone` and unknown origins having no level, the ascending order of all eight, the names, and the two decisions the table encodes — inline above the origin it belongs to, and an important half that is *not* a plain reversal of the normal one |
| `SuiteRoboCssCascadeCollect.c` | 15 | What applies and what could not be used: only the matching rules contribute, cascade order across origins and within one origin, `!important` moving a declaration past its normal siblings, the inline boundary (present / absent / empty / malformed / host failure), and every counter — undecided selectors, at-rule blocks, unparsed rules, destroyed sheets, origin mismatches — plus collection replacing a previous result and releasing everything it allocated |
| `SuiteRoboCssCascadeResolve.c` | 12 | The three keys and the cases that settle them: nothing declared, a single candidate, two declarations of one selector (decided by order), two selectors at one level and different specificity (decided), equal specificity (document order), a stronger level deciding *after* an undecided pair — the case a one-pass resolver gets wrong by remembering its first verdict — importance across origins and inline, identity by property id and by name, provenance, and per-property independence |

### 9.6 — specificity

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssSpecificity.c` | 20 | The three counts and their comparison: the spelled-out matrix (`*`, `div`, `.card`, `#main`, `[disabled]`, `:root`, `::before`, and the compound and complex forms), every simple-selector kind, all six attribute operators, the pseudo-class set and the pseudo-elements that must *not* count as them, `:not()` worth its argument (including nesting and an empty argument), compound and complex accumulation, combinators contributing zero, per-member selector-list values and the most-specific member, escaped identifiers, saturation at the represented maximum, a stress selector that must not wrap, agreement across the four model levels, malformed input that the parser refuses before a calculation can see it, and the API's argument contract |
| `SuiteRoboCssSpecificityCascade.c` | 10 | The key reaching the cascade: a specificity gap deciding a contest, order-independence of that decision, document order still deciding an exact tie, a stronger level outranking specificity, importance at the right stage, a rule whose selector list matched through one member, `:not()` deciding by its argument, a pseudo-element winning nothing, inline still above author, and repeated resolution producing identical results |

### 9.8 — computed values

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssComputedValue.c` | 14 | The typed value without a document: the unit table (which units convert and which are kept, `is_absolute` asked of the table rather than of the converter), the four hex lengths against every wrong length and every non-hex digit, the standard named set in full (every name in the table, so a truncated or misspelled entry fails here and not in a browser), case-insensitivity, `transparent` as a colour that is not a named one, `rgb()`/`rgba()` in both syntaxes with numeric and percentage channels and alpha, range handling (clamped, not wrapped), separator-style mixing refused, the value's lifecycle (copy is independent, move transfers, `equals` distinguishes kinds), the lifetime balance over repeated copies, the seven `computation` shapes (`normalize_*`: keywords lower-cased, absolute lengths converted and relative ones kept, percentages kept, colours normalised, font lists ordered, `PRESERVED` never invalid), and a table of hostile strings driven through the normaliser (`normalize_robust`) |
| `SuiteRoboCssComputed.c` | 15 | The pipeline against the CSS-side element host: a declared property; an inherited one arriving as the **parent's computed value**; a default that an ancestor's inherited value then replaces; a non-inherited property not propagating; `inherit`/`initial`/`unset` through the chain; `var()`, `calc()` and `currentColor` carried and counted; `color: nonsense` marked invalid without dropping the property; custom properties preserved verbatim; inline styles participating; a three-level chain; a **cyclic** parent chain reported as truncated rather than followed; determinism across repeated computations; the argument contract; and an element with no stylesheets at all |
| `SuiteRoboCssDomSelect.c` (`css.dom.computed.*`) | 2 | The same pipeline, all seven stages, over the **visual checkpoint's own document**: two selectors of different specificity producing different colours, a typed colour's channels, `font-size: inherit` landing on the length the body computed (and `width: inherit` on a property that does not inherit by default), a custom property travelling the whole path untouched, `color: nonsense` reported as invalid without corrupting the rest of the style, and a byte-for-byte proof that computing every element's style twice changes nothing the demo paints |

### 9.9 — colours

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssColor.c` | 19 | The module (`css.color.*`): the four kinds and their name table, the kind-aware equality that keeps `currentColor` from comparing equal to a channel set, the cleared state, the six parse outcomes and their names, the standard set (five representative names, then **the whole 148-entry table** walked by index with every entry opaque and every repeated name agreeing, plus case-insensitivity, `transparent` as a colour that is not a *named* one, and an unknown identifier staying unknown), all four hex lengths against wrong lengths, non-hex digits and a space, both argument styles of `rgb()`/`rgba()` with numeric/percentage/mixed channels and numeric/percentage alpha, clamping rather than wrapping (`300`, `-5`, `200%`), one rounding at the end (`51%` → `130`), the mixing of separator styles refused, `hsl()`/`hsla()` with the four angle units, hue normalised (`-120`, `480`, `720`, `-360`, `1e300turn`) and saturation/lightness clamped, `currentColor` typed and the substitution's four cases, the eight deferred spaces reported as deferred rather than refused, `not_a_colour` drawing the line the other way (`sparkle(1)` refused, `10px` not a colour, `red blue` not a colour), 37 malformed declarations driven through the parser with two invariants asserted on every one, and serialization (one spelling per kind, alpha only when it is not opaque, a short buffer refused with the length it needed) — then the integration group (`css.color.computed.*`): twelve spellings of four syntaxes converging on one computed colour each, a typed colour inheriting through two levels with the intermediate override, `background-color` *not* inheriting, `initial`/`inherit`/`unset` on both kinds, the two `currentColor` rules separated by a parent whose colour differs from its child's, a root element whose `currentColor` stays contextual and stays flagged, a `var()` reference carried beside a misspelled colour and an untouched custom property, the whole selector pipeline over a three-level tree, and determinism |

### 9.10 — units and numbers

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssUnit.c` | 18 | The module (`css.unit.*`, `css.numeric.*`): the vocabulary (every enumerator's spelling resolving back to it, the count of 31 real units and 18 viewport units pinned, one representative per family, the whole-set sweep that no relative unit reports itself absolute, unknown enumerators answering nothing rather than the first row, case-insensitivity, `pixels` not being `px`), absolute conversion (every definitional pair, `40Q` = `1cm` *and* `4Q` = `1mm`, zero and negative, `to_px` returning its input for a unit with no factor), resolution (the whole contextual set answering `NEEDS_CONTEXT` with no context *and* with an empty one, font-relative resolved against element metrics and root metrics separately and each refusing without the other, viewport families and the fallback chain with and without overrides, `vmin`/`vmax` picking a dimension, `vi`/`vb` swapping with the writing mode, non-finite magnitudes and non-finite context inputs as `INVALID`, `1e308in` overflow, `NOT_A_LENGTH` for `NONE`, an unwritten `out_px` on every refusal, and a NULL output), the context (a cleared one knows nothing, the setters set their flags, re-init clears, every setter tolerates NULL), the four kinds and their name tables, the two integer questions and the 2^53 bound, kind-aware and unit-aware equality, the constructors (non-finite refused, a length with no unit refused, a failing constructor leaving its output alone), the five outcomes with `DEFERRED` for all six reference functions, `1e999` as `NON_FINITE`, an untouched record on refusal, the token door agreeing with the value door, serialization (whole/fractional/trailing-zero/negative-zero/percentage/unit-canonical/tiny-magnitude, a short buffer refused with the length it needed, a cleared record writing nothing, NULL arguments), 32 malformed declarations driven through with two invariants asserted on every one, and the computed-value integration (every unit family through a real element, `font-size: inherit` landing on the parent's contextual length, `initial`/`unset` on a length-shaped property landing on `auto`, a sheet holding colours and numbers at once, and determinism over a document plus the vocabulary walked in reverse) |
| `SuiteRoboCssDomSelect.c` (`css.dom.unit.pipeline`) | 1 | Every family over the **visual checkpoint's own document**: a contextual unit carried with its unit live on the record (`30em`, `1.25rem`, `50vh`), an absolute length converted wherever it was written (`2.54cm` and `6pc` both landing on `96px`), a non-inherited property nobody declared having no entry, `40q` and the two axes and the dynamic family, `1e2px`, a percentage staying a percentage beside a converted length, an unknown unit reported with the rest of the style unharmed, and the same element computed twice giving the same typed unit and magnitude |

### 9.11 — the box model

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssBox.c` | 13 | The module (`css.box.*`): the side vocabulary with the order walked in sequence and the names read back (a transposed pair would otherwise be invisible), the shorthand table walked **both ways** — every row's four longhands, and each longhand resolving back to its row and its side — plus `border` and `border-top-style` deliberately absent from it, the component mapping for all four arities with a distinct value per side so a wrong answer is visible, trivia skipped so `1px   2px` is two components, the two refusals (five and six components refused as an invalid declaration; `var()`/`env()` refused because substitution would change the count) with `calc()` beside them proving the refusal is about the reference and not the arity, every refusal leaving its output empty, the three value shapes through declaration text (`auto` accepted by `EDGE_OFFSET` and *refused* by `EDGE_INSET`, the three width keywords kept as keywords, `1foo` an unknown unit, `red`/`normal`/`min-content` not in the family's vocabulary), the four longhands arriving from a three- and a two-component shorthand on a real element, the shorthand's own entry surviving beside them, `box-sizing` as a value with no layout, `margin: 0 auto` keeping `auto`, the shorthand-versus-longhand contest in **both orders** plus a shorthand reset that only erases what came before it, specificity and `!important` across two property names, `margin: inherit` and `padding: initial` and `border-width: unset` on a child, non-inheritance proven from both sides (the parent's edges present, the child's absent, an inherited property beside them arriving), a shorthand that cannot be distributed counted and reported while a clean one leaves the counter at zero, fifteen malformed declarations driven through all three shapes with invariants asserted on each, and determinism over a document plus the vocabulary walked backwards |
| `SuiteRoboCssDomSelect.c` (`css.dom.box.pipeline`) | 1 | The box model over the **visual checkpoint's own document**: every arity declared on a real element (two components → vertical zero and horizontal `auto` with `auto` surviving as a keyword, three → the row where `left` borrows `right`, one → all four), the shorthand's own entry present with the author's text, border widths keeping their keywords, `box-sizing: border-box` on a form, `10%` and `2em` staying what they are on a textarea, `padding: inherit` landing on the longhand's own initial because there is no ancestor padding to take, a shorthand and a longhand competing for the same edge on one element decided by document order, and the phase's own acceptance rule last — one sheet declaring `width`, `height`, both shorthands, `border-width` and `color: #336699` on a textarea, adding second so it is the cross-sheet source-order case too, which proves a dimension and a colour are read in the same pass |

### 9.12 — the display vocabulary

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssDisplay.c` | 16 | The module (`css.display.*`) and what the pipeline makes of it: the four values walked in enumerator order with every keyword read back (a table whose rows and enumerators had drifted would only ever show up as a value printing as another one's keyword), the count pinned at four, an out-of-range enumerator answering *nothing* rather than the first row, the outer/inner halves read from the same rows — including `inline-block` being inline-level **and** atomic at once, which is the pair that earns the two columns, and `none`'s inner being NONE because a value with no box has no contents rather than because it is unwritten — the classifications asserted for every value and **cross-checked** so two values cannot both claim the same level, declaration text through the real tokenizer, serialization in the author's own spelling, the four refusals told apart (`flex` is an identifier this build has not added; `20px`, `#fff` and `10%` are not identifiers; `inline block` is two keywords; `rgb(0, 0, 0)` is a reference) each with its own diagnostic, the reference settled *before* the vocabulary is asked, the initial value arriving typed through the style query door, `display: none` as a decided value rather than an absent one, non-inheritance proven from both sides, `initial`/`inherit`/`unset` on a non-inherited property, the cascade's three keys including an inline style, a refused declaration staying *reported* rather than silently becoming `inline`, a malformed sweep with invariants on every case, and determinism over a document plus the vocabulary walked backwards |
| `SuiteRoboCssDomSelect.c` (`css.dom.display.pipeline`) | 1 | The vocabulary over the **visual checkpoint's own document**: one value per level declared on real elements (`body` block, `a` inline, `label` inline-block, `#Notes` none) each read back as a typed enumerator rather than as the keyword's letters, `display: none` on a real element marked `declared` with the style still clean, an element the sheet does not name having **no entry at all** because `display` does not inherit — with the query door answering the property's *own* initial value and marking it as such, and an invalid value on a real element reported rather than replaced |

### 9.13 — the positioning vocabulary

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssPosition.c` | 19 | The module (`css.position.*`) and what the pipeline makes of it: the five values walked in enumerator order with every keyword read back, the count pinned at five, an out-of-range enumerator answering *nothing* rather than the first row, the two flags asserted per row (`relative` and `sticky` being in flow **and** positioned at once, which is the pair that earns two columns), the three classification doors cross-checked so no two values can disagree and an unknown enumerator refused by **all three** — the assertion a shorter implementation that wrote `!in_flow` would fail — declaration text through the real tokenizer, serialization in the author's own spelling plus a buffer one byte short reporting the length it needed, the three refusals told apart (`running()` is an identifier this build has not added; `20px`, `#fff` and `10%` are not identifiers; `absolute fixed` and `rgb(0, 0, 0)` are not a single keyword) each with its own diagnostic, `var()`/`env()` carried and flagged while `nonsense(--p)` is refused, the four insets walked over `robo_css_box_side_count()` with each name read from `CssBoxSideName` so a row written against the wrong side cannot pass, `top: 25%` staying a percentage and `top: auto` staying a keyword and `top: min-content` a mistake, `z-index` accepting `0`, `7`, `-3` and `+12` while refusing `1.5`, `0.0` and `1e2` (`0.0` is the refusal that matters: same magnitude, different token), the initial values arriving typed through the style query door, non-inheritance proven from both sides for all six, `initial`/`inherit`/`unset` on a non-inherited property, the cascade's three keys including an inline style, a refused declaration staying *reported*, a malformed sweep with invariants on every case, and determinism over a document plus the vocabulary walked backwards |
| `SuiteRoboCssDomSelect.c` (`css.dom.position.pipeline`) | 1 | The family over the **visual checkpoint's own document**: `body` `relative`, `h1` `absolute` with `top: 25%`, `button` `sticky`, each read back as a typed enumerator rather than as the keyword's letters, the flags asked on a real element (the `relative` body being in flow **and** positioned at once, the `absolute` heading out of flow), the insets beside them keeping their own types, and the initial value for an element the sheet does not name reaching a consumer through the query door rather than through the container |

### 9.14 — the background family

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssBackground.c` | 31 | Five groups in the order the phase is built. The **vocabularies**: repeat and attachment walked rather than a representative typed out, with each repeat row's two *axes* read from the same row its name comes from (so `repeat-x` being `repeat no-repeat` is asserted rather than assumed), and the count pinned for both. The **components**: an edge written as its axis, a pair as two of them, and equality kind-aware so `left` and `0px` stay different because they are different *values* however equal they look. The **pairs**: what parses, what is refused and why, and the *folding* that makes `center` and `center center` one computed value rather than two, plus `cover`/`contain` being keywords and not `auto`. The **longhands**: all six through the real grammar, every refusal in its own wording, and the registry row whose value is a pair. **Integration**: a real element — initial values through the query door, non-inheritance proved from both sides, `inherit`/`initial`/`unset`, the cascade's three keys, the `background` shorthand's distribution, its **reset** semantics (a longhand the shorthand omits going back to its initial rather than keeping an earlier declaration), and the case where it distributes nothing because the syntax is unsupported |
| `SuiteRoboCssDomSelect.c` (`css.dom.background.pipeline`) | 1 | The family over the **visual checkpoint's own document**: `background-color` on real elements read as a typed colour rather than as letters, a `background-position` pair keeping both components' own kinds, `background-repeat` read back as an enumerator rather than as its keyword, a pixel and a percentage size staying pixel and percentage, and the shorthands distributed onto real elements — which is what makes the distribution pass a fact about the product rather than about the module |

### 9.15 — the border family

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssBorder.c` | 21 | Five groups in the order the phase is built. The **vocabularies**: the ten styles walked in enumerator order with every keyword *read back* (the spelling is the assertion — a table whose rows and enumerators had drifted would still answer `is_known` for all ten), the count pinned, an out-of-range enumerator answering nothing, the ASCII case-insensitivity asserted by *read-back* rather than by knowness, `generates_border` asserted per row so `none` and `hidden` are the two that draw nothing, the three kinds, and the twelve longhand names reached through `for side, for kind` rather than typed out. The **classifier**: all five outcomes walked, and the *order* of the questions asserted — `none` and `hidden` as styles, `thin`/`medium`/`thick` as widths, `red` settled by nothing else, `#fff` and a colour function as colours, a percentage as a width, `var()` deferred, and a nested block invalid. The **expansion**: one `border:` value split into three parts, the `KIND_NONE` a kind the author did not name takes, and each of the three refusals in its own wording. The **shorthands**: the three positional rows through the box table's 1/2/3/4 mapping and the five composites through the registry. **Integration**: a real element — initial values through the query door (including `currentcolor` resolving against the element's own colour), non-inheritance proved from both sides, `inherit`/`initial`/`unset`, and one longhand set by **four different declarers at four different keys** so the cascade is shown to decide rather than a pass order |
| `SuiteRoboCssDomSelect.c` (`css.dom.border.pipeline`) | 1 | The family over the **visual checkpoint's own document**: the twelve longhands on real elements read as typed values rather than as letters, a `border-top-width: thin` staying a keyword while a pixel width stays a length, a `none` style staying distinct from the absence of a style, `currentcolor` resolved against each element's own colour, and `border`, `border-width` and a side shorthand competing for the same longhand — which is what makes the ownership move a fact about the product rather than about the module |

### 9.16 — conformance

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssConformance.c` | 17 | The questions no family suite can ask. The **registry**: every registered property's *initial values* (226 rows at Phase 18.20, from 49 at 9.16) run through the normaliser their own row declares (with the table's length asserted against `CssPropertyRegistryCount`, so a property added later cannot go uncovered), each row carrying the kind, the canonical text and the name so a drifted row fails as a named mismatch rather than a count that happened to agree; the four `currentColor` border colours and the control that is not one; and every row's `computation` checked to be a *named* enumerator rather than an unnamed `"unknown"` that a diagnostic would have printed. The **matrix**: 117 representative declarations (74 before Phase 18's families grew it), one per property, each with its exact computed value *and kind* — first through `CssComputedValueCompute` directly, then on a real element through tokenizer, parser, matcher, cascade, inheritance and computation. The **document**: one realistic sheet with every family asserted on one element and on its child; the three ordering keys (specificity, source order, importance) and the inline level across *families*; both shorthand/longhand orders for all five distributing families; and one longhand set by three declarations at three keys, so the single owner is a fact about the product. The **chain**: one document walked stage by stage with each stage's product asserted; a 42-document malformed corpus through the whole chain for crash safety; determinism asserted over whole *styles* compared by `CssComputedValueEquals` rather than by text; and the allocator balance across repeated rounds. The **capabilities**: every field of the capability struct asserted *by name* against a table that names all of them, plus the family flags (`caps.backgrounds`, `caps.borders`, and since 10.13 `caps.overflow`) that were asserted nowhere in the build before this suite. The **Phase 18 closure** (18.20, three cases): the classification's consequence asked of every one of the 226 rows at once (an element that declares nothing has an entry for every inherited property and none for any non-inherited one), every registered initial value declared on a real element as `name: initialText` and carried through the whole chain to the same computed *kind* the normaliser gives (the reference initials are where the text may differ on purpose), and one document with a representative of every Phase 18 family — flexbox, grid, tables, multi-column, transforms, transitions, animations, container queries, logical properties, writing modes, fonts, text, images, backgrounds, borders and filters — asserted on one element with each family's capability flag checked beside its value |

### 11.9 — the opacity family (a paint phase's prerequisite)

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssOpacity.c` | 8 | The value and its range. `css.opacity.value`: the initial value through its door, the three predicates, and the separating test a NaN needs (`opacity == opacity` is false for exactly one double). `css.opacity.clamp`: both ends inclusive, both infinities clamped to an end, a NaN answered with the **initial** value rather than with zero. `css.opacity.percentage`: `50%` and `0.5` as one value, and the same clamp on both spellings. `css.opacity.serialization`: the fixed-point text form round-tripping, whole values without a decimal point, and a buffer too small refused rather than truncated. `css.opacity.computed.shape` and `.invalid`: the computed kind, the read door answering the initial value for *any other kind*, and the four refusals — `banana`, `1e999`, `10px`, and the clamp that is **not** one of them. `css.opacity.record`: the value record's cleared state, equality by exact number, and `make_opacity` refusing a value outside the range. `css.opacity.registry`: the row the registry actually ships — non-inherited, initial `"1"`, a named computation, and not a shorthand |

### 9.7 — property metadata and inheritance

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssPropertyRegistry.c` | 11 | The property metadata: lifecycle, the standard set (every row classified one way or the other, with a usable name and an initial value that parses), the inherited/non-inherited classification, initial values read back as both text and a parsed tree, custom-property naming and its case rule, lookup, registration (refused names, refused classifications, refused texts, `ALREADY_EXISTS` leaving the first registration alone), that a borrowed definition does not move when the registry grows, applying the set to a caller's table and the `INHERITED` bit it sets, the classification and provenance name tables, and that destroying a registry returns the allocator to where it started |
| `SuiteRoboCssInherit.c` | 6 | The model without the resolver: the CSS-wide keyword table (the positives, and the negatives that matter — `inherit inherit`, `red inherit`, `calc(inherit)`, a quoted `"inherit"`, `12px`, an empty value and a bare token are none of them keywords), the keyword and provenance name tables, the resolved style's lifecycle and accessors (including moving one by value without a double free), the deep value copy (a four-level tree copied and proven independent, disposal in both directions, recopy-replaces rather than leaks, the argument contract) and its lifetime balance over repeated copies |
| `SuiteRoboCssInheritResolve.c` | 20 | Resolution itself: what the element declares; an inherited property taking the parent's value; a non-inherited one not propagating; a child's declaration beating an inherited value; an *intermediate* level beating an ancestor; root behaviour (defaults, no `width` entry, and a root that declares overriding its own default); `inherit`, `initial`, `unset` (one keyword, two behaviours) and `revert` carried-but-reported; custom properties inheriting, being overridable and having no default; an unknown property not inheriting; inline participating; an incomplete cascade propagating its incompleteness; the counters and entry-for-entry determinism; a leak check over repeated resolutions; and the ancestor walk — walking to the root, a host with no `parent` callback, and a chain (and a cycle) bounded and reported rather than presented as a root |

The parser suites are where this documentation's older claim — "they never assert
that something was *parsed*" — stops being true. From 9.3 a parse really happens,
and these suites assert node types, tag-level structure, ranges, tree shapes and
the contents of every list, exactly as the DOM suites assert the DOM.

## 2. The cleared-record contract test

`css.records.cleared_state` exists because §3 of `docs/Architecture.md` — "a
record must be cleared before its first use" — is a precondition the compiler
cannot check, and every caller of `RoboCssCleared` depends on the cleared state
being *exactly* what the init functions produce.

If a cleared record were ever not the same as an init'd one, every caller of
`RoboCssCleared` would be releasing fields nobody set. So the test asserts the
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
written *after* a real crash: a `CssValue` local reached
`CssValueMakeList` uninitialized, the release step walked a `count` of
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

Where a boundary answers `RoboNotSupported`, the suites assert the whole shape
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
scripts/robo test-css                     # the 791 css.* tests only
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
`--filter=css.select.levels` (14), and `--filter=css.dom` (35) for the suites
that run against the real RoboHTML tree — the last two of which are 9.8's, so
that filter is also where the **whole** pipeline runs over the checkpoint's own
document (and, since 18.21, where every Phase 18 family's vocabulary is declared
on a real element).

The 9.5 surface is `--filter=css.cascade` (32) for the three cascade suites —
levels, collection and resolution — and `--filter=css.dom.cascade` (4) for the
part of it that drives the real tree. `css.cascade` does not pick up
`css.dom.cascade`, because the prefix is matched against the whole name.

The 9.6 surface is `--filter=css.specificity` (30), which is both suites:
twenty value/calculation cases and ten that drive the cascade with them. The real
tree's one case is `--filter=css.dom.specificity` (1).

The 9.7 surface is `--filter=css.inherit` (26), which is the model and the
resolver together, and `--filter=css.registry` (11) for the property metadata.
`css.inherit.resolve` (17) and `css.inherit.chain` (3) are the two halves of the
resolver, and `css.inherit` picks both up plus the model's six.

The 9.8 surface is two filters that do **not** nest: `--filter=css.computed.`
(15) is the pipeline plus the 9.1 container's two boundary cases, and
`--filter=css.computedvalue.` (14) is the typed value.
The trailing dot matters, because `css.computed` is a prefix of
`css.computedvalue` — but `css.computed.` is not, which is why the documented
form carries it.

The 9.9 surface is `--filter=css.color` (19), the whole suite, plus
`--filter=css.color.computed.` (7) for the integration half. The remaining twelve
have **no single prefix of their own**, and that is worth stating rather than
leaving as a puzzle: the integration names are `css.color.computed.*`, so
`css.color.` is a prefix of them as well and catches all nineteen. The module's
twelve are reached by their own names (`css.color.named_table`, `css.color.hsl`,
and so on), or by running the suite and reading the twelve that come first.

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
  findings, and `--filter=css.color` is 19/19 under the sanitized binary. Phase 9.9
  introduced no memory-safety or undefined-behaviour defect — which is not a
  coincidence: the whole module owns nothing, so the only lifetime question it can
  raise is whether a *copied* colour record stayed valid across an inheritance
  step, and that is what `css.color.computed.inheritance` asks.

Run the whole suite rather than the `css.` subtree and the sanitized binary may
report **one** failure that is not a sanitizer result and not this subsystem's: `window.host.keyboard` asserts `WindowRequestFocus(id) == RoboOk`, and
Windows declines `SetForegroundWindow` for a process not entitled to the
foreground, so from a detached shell the call returns `RoboNotSupported` and
the case fails. It is intermittent — in the 9.3 validation runs the plain build
passed 3/3 and the sanitized one 1/2 — and it is documented in `docs/Build.md`
§6. No `window.host.*` case exercises any RoboCSS code, and no ASan or UBSan
diagnostic accompanied it.

One real defect was found by the sanitizers during 9.1 and fixed rather than
suppressed:

> `CssStylesheetParse` dereferenced its `out_error` parameter
> unconditionally when reporting the not-yet-implemented parse, even though
> `CssErrorSet` tolerates a NULL destination and the header promises the
> record is optional. ASan reported the access at `0x0` when
> `css.sheet.parse_boundary` called it as `CssStylesheetParse(id, NULL,
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
records reached `dispose`/`copy` without being `RoboCssCleared` first. The
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

> **Three matcher entry points nobody called.** `CssSimpleSelectorMatches`,
> `CssCompoundSelectorMatches` and `CssComplexSelectorMatches` are
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
describes nothing and is gone: `RoboCssCascadeVerdictCount` is 2, a value that
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
> `CssSpecificityEqual` on the two candidates before asserting the winner,
> so "this was decided by order" is a fact the test establishes rather than an
> assumption it leans on.

## 11. What the 9.7 suites found

One real product defect — a NULL dereference that the sanitizer run caught and the
plain build did not — plus four mistakes in the tests themselves and one in the
fixtures that a combined run exposed.

> **`CssPropertyRegistryCreateStandard` dereferenced its output.** The
> function wrote `*out_registry = registry` without ever checking that
> `out_registry` was non-NULL, so `create_standard(NULL, NULL)` — the argument
> contract every other door in the subsystem honours — crashed instead of
> returning `RoboInvalidArgument`. The plain build's full-suite run died with a
> segfault whose position could not be located, because the harness does not flush
> stdout on the way out; the sanitizer run pinned it to the exact line and the
> exact call. It is the clearest argument this project has for running the
> sanitized binary even when the plain one looks green.

> **The slot table is why a combined run and a filtered run disagree.**
> `RoboCssMaxStylesheets` is a fixed 64-entry module table, and a suite that
> created a sheet per test and never released it exhausted it partway through the
> combined `css.` run — after which *every* sheet creation failed and unrelated
> tests failed for a reason with nothing to do with what they test. Every suite
> passed in isolation, which is exactly what made this hard to see. The fix is in
> the fixture, not in the constant: the cascade fixture now owns the sheets it
> makes and releases them in its `end`, so the bound stays a real product bound
> rather than a number raised until the tests stopped complaining. A test that
> destroys a sheet itself must now do so *before* the fixture's end.

> **Three expectations were off by one against the resolution contract.** The
> `unset` case, the counters case and the ancestor-walk case each counted a
> property twice or subtracted it when it was already supplied: `unset` on an
> inherited property lands *inside* the inherited family and so adds no entry (it
> is `inherit` on `width`, a property *outside* the family, that adds one), a
> declared inherited property means the family has one fewer default, and a child
> that declares one family property still inherits the other fifteen from its
> parent. In every case the implementation matched the header and the expectation
> did not.

> **A hand-built token is bound to a source.** The value-copy tests built their
> tokens with a NULL source, which the constructor refuses — a token with no source
> is not a token. The suite's own builder helper now goes through a real
> (`set_memory`, borrowing) source, which is the same way a parsed token is bound.

> **A host that cannot answer `parent` is not a host with no view.** The
> without-a-host case cleared the whole element host, which takes matching away
> too, then asserted that the sheet still matched. It now installs a host with the
> view but no `parent` callback — the case the header actually describes. And its
> argument-contract assertion expected a CSS error kind for a bad argument; the
> style domain records argument failures with kind `NONE` and reports them through
> the return code and the record's `code` (the cascade door does the same), so the
> test now asserts the code.

## 12. What the 9.8 suites found

Three real product defects. Two of them had survived a green `--filter=css.`
build in a state where the new suites were compiled but **not registered** — the
build script's source list and `Tests/Main.c`'s registrations are two places, and
a new suite file has to be in both. Until it was, 9.8's assertions were files on
disk and nothing more.

> **`CssComputedValueCopy` copied the bytes but not their length.** The
> pointer was reassigned and `text_length` was explicitly zeroed before the
> allocation, and only the allocation branch memcpy'd — so a copied value had a
> text pointer with a length of zero, which reads as *empty*. `equals` said two
> identical values differed, and `text` returned a zero-length slice for `auto`.
> In the pipeline that is how a keyword would reach a child as no keyword at all:
> inheritance copies the parent's computed value, so `div { color: red }` handing
> `red` to a child went through this function. The suite that found it is
> `css.computedvalue.lifecycle`.

> **`CssUnitIsAbsolute` asked the converter a question it cannot answer.**
> It was written as `CssUnitToPx(1.0, unit) != 0.0`, and `unit_to_px`
> returns *the value it was given* for a unit with no fixed factor — so `em`, `vh`
> and the empty unit all reported themselves absolute. `em` and `vh` would have
> been silently converted to pixels in the computed value, which is the exact
> thing 9.8 exists not to do. The predicate now asks the table whether the factor
> is present.

> **`rgb()` could not parse the space syntax at all.** The argument splitter
> tracked "the last component was an argument" to enforce separators, and skipped
> whitespace — so two space-separated numbers looked adjacent, and `rgb(255 0 0)`
> was refused while `rgb(255, 0, 0)` was accepted. The fix is not a special case:
> whitespace *is* a separator in CSS's modern syntax, and the splitter now records
> which of the two styles a call started with and refuses a second one. That is
> what makes `rgb(255, 0 0)` malformed, `rgb( 1 , 2 , 3 )` legal, and
> `rgb(1 2 3 4)` — four numbers and no slash — a call with an alpha separator it
> never wrote.

Five expectations were wrong rather than the implementation, and each was worth
checking rather than bending the code to:

* **`#ff00` is a colour.** Four digits is `#rgba`, so the case meant to prove a
  malformed hex was refused was proving a *valid* one was. It now asserts
  `#ffff0000` and the malformed case is `#ff`.
* **A length-shaped property may only be given a length keyword.** The
  implementation accepted *any* identifier, so `width: red` produced a keyword the
  layout phases could only fail on. It now tests a closed set — the union of the
  keyword lists of the properties that share the shape — and the keyword test's
  `10` expectation was corrected to the message that says which of the two
  problems it is (`not an identifier`, not `not a single identifier`: `10` *is* one
  component).
* **`css_allocator_ensure_enabled` returns whether *this call* turned the tracker
  on**, not whether it is on. The lifetime-balance test asserted `1` where a
  different suite may already have enabled it; it now asserts the invariant it
  actually needs (`MemDebugIsEnabled()`), and reports the return value to
  `css_allocator_restore` where the helper's contract puts it.
* **Three argument-contract cases passed a computed value where a specified value
  belongs.** The compiler caught them once the suite was registered; they now test
  NULL arguments and the unknown-`computation` sentinel, which is what the header
  promises.

## 13. What the 9.9 suites found

One real product defect, and the one that would have been easiest to ship without
noticing: the suites that would have caught it had to be *registered* first.

> **A `var()` in a colour position was reported as a mistake.** `color:
> var(--brand)` reached the colour parser, which called it non-colour syntax and
> returned `INVALID` — so a legal reference to a colour produced the same computed
> value as `color: nonsense`, and the diagnostic said `not a colour`. The colour
> parser was right about the syntax and wrong about the author: the value is not a
> colour *yet*, it is a reference to one. `computed_compute_color` now settles that
> first, asking whether the value is a single unresolved function (`var`, `env`,
> `calc`, `min`, `max`, `clamp`) and carrying it with its tokens and the
> `UNRESOLVED` flag if it is — which is what every other value shape already did,
> and what makes the two diagnostics genuinely different. Found by
> `css.color.computed.reference_and_invalid`.

Three expectations were wrong rather than the implementation, and each is worth
writing down because all three are the kind of mistake a test is supposed to make
*visible*:

* **`#f00f` is `#ff0000`.** An opaque alpha nibble normalizes away, which is the
  representation agreeing with the `#rrggbb` spelling of itself. The assertion
  expected `#ff0000ff`; `css.color.hex` now pins `#ff0000` and the alpha channel
  beside it.
* **`rgb(51%, 40%, 60%)` is `#826699`.** The percentage case had the *numeric*
  channels' expected value copied into it — `#336699` is `rgb(20%, 40%, 60%)`.
  The corrected pair now proves the distinction: `20%` is exactly 51, and `51%` is
  130.05 rounded to 130, which is `css.color.rgb`'s one-rounding rule.
* **`hsl(210, 100%, 20%)` is `#003366`.** The case expected `rgb(0, 68, 102)`,
  which is 20% *lightness* read as if the green channel were the sector's endpoint
  rather than its intermediate: at 210° the blue channel is lightness plus half the
  chroma (102) and the green is the intermediate (51). The suite now asserts the
  same colour through three syntaxes — `hsl()`, `#rrggbb` and `rgb()`.

One helper was broken in a way that made a *failure message* lie: `css_color_same`
passed `NULL` for the serialization length, which `CssColorWrite` refuses,
so the two buffers it printed as "the colours that differ" were uninitialised
stack. It now checks both writes and blanks a buffer that failed, because a
diagnostic that names stack noise as a colour is worse than no diagnostic.

## 14. What the 9.10 suites found

One real product defect, and one defect the *delegation* introduced — which is the
more interesting of the two, because it was invisible until the suites ran.

> **A length-shaped property rejected every keyword.** The computed-value layer's
> length path moved its keyword handling inside the branch guarded by "there is no
> single token". `auto` is one IDENT *token*, so it never entered that branch: the
> identifier fell through to the numeric parse, which is right to call an identifier
> not a number, and the developer was told `not a length` when the property's grammar
> simply did not accept the keyword they wrote. Seven 9.8 tests failed on it —
> `l: auto`, `h: auto`, `font-size: medium`, `width: auto` — and every one was the
> same defect wearing a different property's clothes. The fix asks the property's
> keyword list **before** the numeric vocabulary, and the comment saying why is now
> load-bearing rather than decorative.

> **A whole `-0` printed as `-0`.** The serializer normalized negative zero inside
> the *fractional* branch, and `-0.0` has no fraction to strip — `%.0f` renders it
> whole — so the sign survived on a value that is zero. The header documents "a
> negative zero is `0`, because it is zero", so the rule was right and its placement
> was wrong. It now runs after both branches, which is where a rule about the
> *rendered digits* belongs. Found by `css.numeric.write`.

Four expectations were wrong rather than the implementation, and all four are the
kind of mistake worth recording:

* **`40Q` is `37.795px`, not `9.6px`.** `Q` is a quarter of a *millimetre*, so
  forty of them are a centimetre. The assertion had a factor of four applied twice.
  The suite now asserts the relationship both ways round — `40Q` is `1cm` and `4Q`
  is `1mm` — precisely because one direction alone would not have caught it.
* **`1e300in` does not overflow.** 9.6e301 is a finite `double`; the case was chosen
  to prove that a product which overflows is reported `INVALID` rather than as
  infinity, and the magnitude did not overflow, so it proved nothing. It now uses
  `1e308` and asserts that `1e300` *resolves*, so the boundary is pinned from both
  sides.
* **There are 18 viewport units, not 16.** Six in the default family — `vw`, `vh`,
  `vmin`, `vmax`, `vi`, `vb` — and four in each of the three overridable ones. The
  count is still pinned as a number, so a family added without its units fails here.
* **A helper asserted the module with the module.** The malformed-input sweep
  checked finiteness with the runtime's own predicate, so the assertion would have
  agreed with the code under test by construction. It now spells the finiteness test
  out, which is what makes it an assertion about the parser rather than about
  itself.

A note on what the phase *did* catch early, which is the reason it is worth
writing down: `CssNumericFromValue`'s contract — exactly one significant
component — was already correct, and the reason `10 px` and `1.2.3` are `INVALID`
rather than a silently-halved number is that the tokenizer is lossless and the
component check happens before any numeric reading. The malformed sweep exists to
keep that true rather than to establish it.

## 15. What the 9.11 suites found

No product defect, and the reason is worth recording as plainly as a bug would be:
the phase's risky part — deciding which of a shorthand and a longhand wins an edge —
was built by *asking the cascade* rather than by re-deriving its three keys, so the
four directions the suite pins (document order both ways, specificity, importance) all
went through code that was already correct and already tested. Building the second
implementation is what would have produced a finding here.

Five expectations were wrong rather than the implementation, and three of the five
are the same shape of mistake:

* **A computed `PRESERVED` value renders with spaces, not commas.** The assertions
expected `8px,_,12px,_,4px` — the *declaration shape* helper's spelling — where the
computed serializer writes `8px 12px 4px`. The two helpers format the same tree
differently and on purpose, and reaching for the wrong one is easy enough that it is
worth knowing which is which.
* **An id selector does not match a class.** `css_host_add(host, "div", "panel",
  "box")` gives the element `id=panel` and `class=box`, so `#box { margin: 10px }`
  never matched and the *type* selector's `margin-left: 20px` won — which looked like
a precedence bug and was a fixture bug. The test now uses `#panel` and asserts the
same contest for real.
* **An absent entry's provenance is `<absent>`, not `<none>`.** `css_inherit_source`
  reports the absence rather than inventing a source, and `<none>` belongs to
  `SOURCE_NONE`, which is a *present* entry with no provenance — the same distinction
the value helper draws, and one that has to be got right or "the child has no margin"
reads as "the child has a margin from nowhere".
* **An inherited default is inherited from the parent, not defaulted again.** A
  child's `color` took the parent's value, and the parent's value *was* the registry's
default — so the child's provenance is `parent`, not `default`. The unit suite gives
  the same property `default`, because there the child has no parent style at all;
  both are right and the difference is exactly the parent's presence.
* **A context's sheets are shared by every element in it.** The "and a shorthand that
  *does* distribute leaves the counter at zero" half of the unexpanded-shorthand test
  re-used the fixture whose sheet already held the two undeployable declarations, so
  the counter legitimately stayed at one. It now uses a fixture of its own, which is
the correct fix — the alternative would have been asserting the counter was zero on
input that should set it.

## 16. What the 9.12 suite found

No product defect again, and here the reason is structural rather than lucky: every
question the module answers is a lookup in one four-row table, so there is no place a
keyword spelling can be compared wrongly and no branch that can disagree with a
sibling. The failure the phase *was* exposed to — a value outside the vocabulary
slipping through as valid — is the thing the table cannot do, which is why the refusal
cases are the largest group in the suite (`flex`, `grid`, `table`, `contents`,
`inline-flex`, `inline_block`, `20px`, `#fff`, `10%`, `inline block`, `rgb(0, 0, 0)`) and
why each asserts its own diagnostic rather than a shared "invalid".

Three expectations moved instead, and the first is the one worth recording:

* **The registry suite did not have to change at all.** 9.12 gave a row that already
existed a new shape and added an eleventh shape to the column. The 9.11 table tests
went green untouched because they *derive* — every row classified one way or the other,
every initial value parseable — instead of pinning a shape per property name or a count
of shapes. That is a property of how they were written, not an accident, and it is the
only way a table suite can survive its table growing.
* **A property's initial value arrives typed on that property's own shape.**
  `css.dom.computed.pipeline` served a rule through the real tree and asserted that the
  property's initial value came back as the kind `keyword`. It did, and correctly: that
  is what an initial value *is* until its property has a shape. The property that test
  happens to read is `display`, so the moment the row gained a shape the same door began
  answering a `DISPLAY` value holding an enumerator. The assertion was right about the
  pipeline and wrong about the property; the suite now asserts the enumerator, which is
  strictly more than it asserted before — and it is a small illustration of why the
  *pipeline* and the *row* are separate things a reader should keep apart.
* **A closed vocabulary is fuzzed for its refusals.** The computed-value malformed sweep
  gained the display shape so it is driven by the same hostile strings as the other
  shapes, with the invariant that matters here stated explicitly: no string may produce
  a display value that is not one of the four.

## 17. What the 9.13 suites found

One real defect, and it was in a **diagnostic** rather than in a value. The box model's
`EDGE_OFFSET` shape had carried the message *"not a margin length or auto"* since 9.11,
which was accurate while `margin` and its three relatives were its only row. 9.13 put the
four insets on the same shape — deliberately, so the two families could not normalise a
percentage differently — and at that moment the message began naming one of its two
families. Because the shape is shared, the *identity* of the property is not available at
the point the message is written, so the only honest fix was to describe the grammar:
*"not a length, a percentage or auto"*. Three assertions in `SuiteRoboCssBox.c` moved with
it. This is the shape of defect a shared shape invites, and it is worth recording for the
reason that it is invisible in the suite that caught it: the box suite went red on the
box's own message while the *product* was correct.

The other expectation that moved is the one 9.12 predicted:

* **The registry suite pins counts, and the counts moved.** It asserted 39 properties and
  23 non-inherited; 9.13 adds five non-inherited rows (`top`, `right`, `bottom`, `left`,
  `z-index`) and `position` keeps its row with a new shape, so the numbers became 44 and
  28. The structural assertions beside them — every row classified, every initial value
  parseable, every name usable — went green untouched, which is the part that means the
  table is still coherent. A count is a fact about the phase, not about the table.

Three things the suite could not have caught and were written to be caught:

* **The name collision.** The module surface is `positioning_*` and not `position_*`,
  because `CssPosition` is already the **source position** type (offset, line,
  column). The suite walks the vocabulary and reads every name back, so a stray `position`
  spelling would have surfaced as a compile error rather than as a behaviour — which is the
  good outcome, and it happened before any test ran.
* **The insets are walked between the module and the registry.** The suite does not hand-list
  `top`, `right`, `bottom` and `left`; it walks `robo_css_box_side_count()` and reads each
  name from `CssBoxSideName`. A registry row written against the wrong side is
  therefore a failure, and it is the kind of transposition that would otherwise look like a
  passing test: `top: 10px` computing to `10px` is unremarkable, and computing to `right`
  is what a hand-written list cannot see.
* **`z-index: 0.0` is refused on purpose.** It is the one refusal in the phase that a
  reasonable implementation gets wrong while every value it produces is *numerically*
  correct. The suite asserts it beside `1.5` and `1e2` so the rule being tested is visibly
  "the token, not the magnitude".

The sanitizer run is the same in kind as every earlier phase: the whole suite under
AddressSanitizer and UndefinedBehaviorSanitizer returns to the baseline allocator count,
with no report from either, which is what makes the two-sheet and repeated-resolution
scopes in the new suite meaningful rather than decorative.

## 18. What the 9.14 suites found

Four things, and the first three are **gaps** rather than mistakes — the phase's own tests
are what surfaced them:

* **Five computation shapes had no name.** `CssPropertyComputationName` fell to
  `"unknown"` for the five backgrounds shapes. A name is what a diagnostic prints, so an
  unrecognised shape is a message nobody can act on — and nothing else in the build reads
  that function, which is why only a test walking the set can see it.
* **The parse-outcome vocabulary had no doors.** `CssBackgroundParseIsKnown` and
  `_parse_name` were declared in the header and had no definition. That is a link error
  rather than a silent one *because the suite walked the set*; a suite that had typed out
  the outcome it cared about would have left the other three undefined.
* **A hex colour in the shorthand was refused.** `background: #fff` classified as belonging
  to no longhand while `background: white` did not, because the classifier's HASH case fell
  to the default. Four spellings of one colour reaching one longhand is now asserted directly.
* **The suite caught a bug in itself.** `css.background.robust` compares two computations of
  one malformed input and compared `b.kind` against `a.kind` *after* disposing `a`. Reading a
  disposed record's kind is exactly the class of defect that suite exists to hunt, and it is
  worth recording that it found one in its own author.

One expectation moved, and it is the count 9.13 warned about:

* **The registry suite pins counts, and the counts moved again.** It asserted 44 properties
  and 28 non-inherited; 9.14 adds five non-inherited rows (`background-image`,
  `background-repeat`, `background-position`, `background-size`,
  `background-attachment`), so the numbers became 49, 16 inherited and 33 non-inherited.
  `background` and `background-color` were already rows — the phase *distributes* the
  shorthand rather than registering it again, and a shape change is not a new property. The
  structural assertions beside them — every row classified, every initial value parseable,
  every name usable — went green untouched, which is again the part that means the table is
  still coherent rather than merely the right size.

Three things the suite was written to catch:

* **A pair has two spellings, and only one is stored.** `background-position: center` and
  `background-position: center center` are the same computed value, so the *folding* happens
  once at parse time and the suite asserts the two arrive equal rather than asserting the
  folding is attempted twice. The failure this guards against is an implementation that
  keeps the source's arity, which would make two equal styles compare unequal and would
  only show up at the phase that starts comparing computed styles.
* **A component's kind is part of its identity.** `left` and `0px` are equal as offsets and
  *not* equal as values, and the suite asserts the inequality on purpose — the phase that
  resolves them against a box is entitled to two different answers, and an equality that
  said otherwise would erase the distinction before anyone could use it.
* **The shorthand's reset is asserted, not implied.** `background: red` sets the five
  longhands it does not name back to their **initial** values rather than leaving an
  earlier declaration standing, so a later longhand and an earlier longhand are not the same
  document. This is the one shorthand behaviour that a "populate what you can"
  implementation gets wrong while every value it writes is correct.

## 19. What the 9.15 suites found

Four things, and the first three are **gaps** rather than mistakes:

* **The style set had no shape until it was walked.** `border-style` needed a shape of its
  own — a style is neither a keyword nor a length — so `BORDER_STYLE` became the fourteenth
  computed-value kind. The *read-back* is what proves it: a table whose rows and enumerators
  had drifted would still answer `is_known` for all ten, and would only show as a value that
  printed as another one's keyword.
* **`none` is both a value and an absence.** `CssBorderStyleFromSlice` answers
  `NONE` for the keyword `none` *and* for "not one of the ten", because `none` is a real
  value and there is one enumerator for "nothing". The suite asserts the trap directly
  (`css.border.style.none_trap`) rather than leaving the property layer to rediscover it —
  a `border-style: banana` that computed to `none` is a border that vanishes while every
  declaration looks like it worked.
* **The box table had a row nothing distributed.** `border-style` and `border-color` were
  absent as rows because nothing before this phase spread them over four sides. Adding them
  is a two-line change *once the table exists*, which is the argument for having the table.
* **The distribution had two owners, and no test would have caught it.** 9.11's
  `border-width` row was distributed by the box model's pass. That was correct in isolation
  and wrong in a build where `border` and `border-top` cover the same longhands, and the
  failure mode is not a crash — it is a *last-writer-wins* answer that depends on pass
  order. The fix is the phase's architecture (one owner, and one predicate the box pass
  asks), and `css.border.cascade` plus `css.dom.border.pipeline` are what now hold it.

One expectation moved, and it is the count 9.14 warned would move again:

* **The registry suite pins counts, and the counts moved again.** It asserted 49 properties,
  16 inherited and 33 non-inherited; 9.15 adds fourteen non-inherited rows — the four
  `border-*-style`, the four `border-*-color`, and the six shorthands `border-style`,
  `border-color`, `border-top`, `border-right`, `border-bottom` and `border-left` — so the
  numbers became **63, 16 inherited and 47 non-inherited**. `border-width` and `border` were
  already rows: the phase *distributes* them rather than registering them again, and a shape
  change is not a new property. The structural assertions beside them — every row classified,
  every initial value parseable, every name usable — went green untouched.

One documented flake, unchanged and not this subsystem's: running the whole suite from a
shell Windows does not consider entitled to the foreground fails `window.host.keyboard`,
which asserts `WindowRequestFocus(id) == RoboOk` and gets `RoboNotSupported` when
`SetForegroundWindow` is declined. It is intermittent (it passed 3/3 standalone and in both
of the plain and sanitized full runs of this phase) and no `window.host.*` case reaches any
RoboCSS code. See `docs/Build.md` §6.

## 20. What the 9.16 suite found

Two things, and both are the kind only a whole-product question finds:

* **Two capability flags were documented and asserted nowhere.** 9.14 and 9.15 each recorded
  that their flag had flipped to 1. `css.capabilities` stopped at `positioning` and then
  tested the two flags that are 0 (`colors`'s siblings aside), so `caps.backgrounds` and
  `caps.borders` could each have been 0 in the shipped build while every family suite stayed
  green — because a capability flag is a promise about the *build*, and a family suite tests
  its family. The fix is a table naming every field of the struct, walked field by field, so
  a field added later is a compile-time omission rather than a silently untested flag.
* **A row can compute `INVALID` for every element of every document and nothing would say so.**
  This is the invariant the 63-row initial table exists to state: `css.registry.initial_values`
  asserts the initial value's *shape* — a 9.1 fact about a value tree — and says nothing about
  what that shape computes to under the row's own `computation`. A drifted pair produces an
  `INVALID` on every element, declared or not, and the only other place that would show is a
  render, which does not exist yet.

The suite found two bugs in **itself** as well, which is worth recording because both are the
failure mode a conformance suite is most prone to: a cascade case whose selector names a class
no element had, so all three declarations it compared were absent rather than competing, and a
per-context inline stub that answered every element instead of the one under test, so an
assertion about a child was really an assertion about the root. A conformance suite that uses
the same plumbing as the families can fail the same way the families can; the difference is
that here the assertion is cheap enough to add the element that makes it mean something.
