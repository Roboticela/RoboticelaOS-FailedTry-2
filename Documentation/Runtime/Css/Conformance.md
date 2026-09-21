# RoboCSS Conformance (Phase 9.16)

Phase 9's other forty-one suites each know **one family**. `css.border.*` asks what `border-top-style: solid` computes to; `css.background.*` asks what `background: red` resets; `css.unit.*` asks what `1in` is in pixels. Every one of them is trustworthy for the same reason it was possible to write: the question fits inside one module.

None of them can ask the questions that are only true of the *product*. This phase is those questions, and it adds **no production code at all** — one test file, and a version bump because the build's *promises* changed rather than a header.

```
css/conformance.registry      every registered property computes
css/conformance.matrix        every property, one declaration, one exact answer
css/conformance.document      one document, every family at once
css/conformance.pipeline      malformed bytes, identical bytes, lifetime
css/conformance.capabilities  what the build claims about itself
```

| Goal | What it delivers |
|---|---|
| 9.16.1 the registry computes | Every registered initial value (66 rows since 10.13's overflow family), run through the normaliser their own row declares |
| 9.16.2 the reference initial | The four `currentColor` border colours, and the control that is not one |
| 9.16.3 the computation vocabulary | Every row's `computation` is a known, *named* enumerator |
| 9.16.4 the matrix | 71 representative declarations, each with its exact computed value **and kind** |
| 9.16.5 the matrix through the pipeline | The same 71, on real elements, through the real chain |
| 9.16.6 the document | One realistic sheet, every family asserted on one element, plus the child |
| 9.16.7 the ordering keys | Specificity, source order, importance and the inline level, across families |
| 9.16.8 shorthand and longhand | Both orders, for all five distributing families |
| 9.16.9 the three declarers | One longhand, three declarations, one owner |
| 9.16.10 the whole chain | One document, five stages, each stage's product asserted |
| 9.16.11 the malformed corpus | 42 hostile documents through the whole chain |
| 9.16.12 determinism | Whole styles compared, by structure and not by text |
| 9.16.13 lifetime | The pipeline returns the allocator, every round |
| 9.16.14 the capability flags | Every field of the struct, asserted by name |

## 1. The invariant no family suite can state

`css.registry.initial_values` asserts the **shape** of every registered initial value — that `background-position` starts at `0%,_,0%` and `border` at `medium,_,none,_,currentcolor`. That is a Phase 9.1 fact about a value *tree*: the tokens and their nesting.

It says nothing about what those initials **compute to**. A property whose `computation` column and whose `initial_text` had drifted apart — a `display` shape on `border-top-width`, a `colour` shape on `padding` — computes `INVALID` for every element of every document, whether or not the author declared it, and *nothing else in the build would notice*:

```c
/* css/tests/SuiteRoboCssConformance.c */
RoboCheckEqInt(CssComputedValueCompute(definition->computation, initial,
                                                 &value, NULL), RoboOk);
RoboCheck(value.kind != RoboCssComputedValueInvalid);
```

The table is 63 rows in the registry's own order, and each row carries three facts: the **kind**, the canonical **text**, and the property's **name**. The name column is what makes a drifted row fail as a named mismatch rather than as a count that happened to still agree, and the table's own length is asserted against `CssPropertyRegistryCount` so a property added later cannot go uncovered:

```c
RoboCheckEqInt((int)(sizeof(css_conformance_initials) /
                        sizeof(css_conformance_initials[0])),
                  (int)CssPropertyRegistryCount(registry));
```

### The two spellings, and why the table has both

```
border          preserved   medium none currentcolor     ← the registry's tokens
border-top-color colour      currentColor                 ← the colour module's spelling
```

A **preserved** value is the tokens the registry wrote, so it keeps the source's `currentcolor`. A **colour** value is the colour module's canonical rendering, so it is `currentColor`. The same word appears one way in five rows and the other way in six, and a build that normalised a shorthand's tokens — or preserved a colour's — shows up as exactly that diff. It is the sort of thing a table finds and a prose test does not.

## 2. The reference initial, and its control

`border-*-color` starts at `currentColor`, the registry's only initial value that is a **reference** rather than a value. The record carries a contextual colour *and* is flagged unresolved, and the two facts are asserted together because either alone is ambiguous: a resolved `currentColor` and an unresolved one are the same kind.

```c
RoboCheckEqInt(value.kind, RoboCssComputedValueColor);
RoboCheckEqInt(value.unresolved, 1);
RoboCheckEqInt(CssColorIsCurrent(value.color), 1);
```

Beside it sits the control: `background-color`'s initial is `transparent`, which is a *colour*, so it is the same shape, same layer, and **not** unresolved. A test with only the four border colours could pass for a build that marked every colour unresolved.

## 3. The matrix: 71 declarations, one answer each

The registry's initials are one document. The matrix is the other: what an **author** writes.

```c
{"width", "width: 50%", "percentage", "50%"},
{"width", "width: 1in", "length", "96px"},
{"width", "width: 2em", "length", "2em"},
{"z-index", "z-index: 1.5", "invalid", "not a whole number"},
{"border-top-style", "border-top-style: hidden", "border-style", "hidden"},
```

Both columns matter and neither is redundant. The text is what a consumer prints. The **kind** is what a consumer dispatches on — `width: 50%` printing as `50%` is unremarkable, and being a `percentage` rather than a `length` is the entire contract of 9.10. A test that asserted only the text would pass for a build that had started resolving percentages into pixels.

The refusals are asserted by their **wording**, which is where the matrix earned its keep twice over. Five of them were not what a summary would have guessed:

```text
color: banana                unknown colour name        (not "not a colour")
z-index: 1.5                 not a whole number         (not "not an integer")
padding-top: auto            not a padding length       (not "not a length")
background-repeat: banana    not a background repeat value
border-top-style: banana     not a border style value
```

Each names the *grammar that refused* rather than the general shape, which is the difference between a message a developer can act on and one they cannot.

## 4. The matrix is about the product, not the normaliser

§3 drives `CssComputedValueCompute` directly. That is a statement about one function. §4 puts the same 66 declarations on real elements through the whole chain — tokenizer, parser, selector grammar, cascade, specificity, inheritance — and asserts the same answers arrive:

```
div.c0 { color: red }
div.c1 { color: #336699 }
...
```

The sheet is **composed from the same table** the expectations come from. A second hand-written sheet would be a second thing to keep in step, and the point is that *this* table survives the pipeline. Two assertions ride along with each value:

- the `DECLARED` flag, asked as the flag rather than as the rendered set, because a refused declaration is `declared+invalid` and a reference is `declared+unresolved` — both declared, and the flag is the half this test is about;
- `incomplete_count > 0` for every INVALID row, which is how a consumer sees how much of a style it can trust.

The host holds at most `CSS_HOST_MAX_ELEMENTS` elements, so the rows are walked in chunks of one sheet and one element per row. The class names are held in an array **outside** the loop, and that is load-bearing rather than stylistic: a host element *borrows* the bytes it is given, so a name in a per-iteration local is dangling by the time the sheet is added — which fails as a handful of elements silently matching nothing, not as a crash.

## 5. One document, every family at once

The per-family DOM pipelines each assert their family. This is the only place they are asserted **together**, and that is a different claim: a `.card` is a display, a position, an inset, a size, four margins, four paddings, four border widths, a border style, a border colour, a background and a text colour. A build in which any one family disagreed with the others would still pass twelve separate suites.

```css
div.card { display: block; position: relative; top: 10px; width: 50%;
           margin: 10px 20px; padding: 5px; border: 1px solid black;
           background-color: #ffffff; color: #336699; z-index: 3 }
```

### What the child asserts, and it is not what a reader expects

```
a NON-inherited property nothing declared   has no entry at all
an INHERITED property                       is always present
```

So on the label, `margin-top`, `padding-top`, `background-color` and `border-left-style` are all `<absent>` — while `letter-spacing`, which nothing mentions anywhere, is `normal` with the `default` flag. That is not an inconsistency: an inherited property always *has* an answer (the parent's, or the registry's initial), and a non-inherited one has an entry only when someone asked for it. The distinction is what makes "this element has no border" a different statement from "this element has a border of zero width", and it is worth stating explicitly because the intuitive reading is the wrong one.

The child also proves the *pair*: `color` arrives `inherited` at the card's computed `#336699`, and the label's own `border-top-style: dashed` arrives `declared`.

## 6. The three declarers, and the ownership question as a product fact

`border: 1px solid #111111`, then `border-top: 2px dashed`, then `border-top-width: 4px` — all three on one element, at one specificity, so source order decides. The four other sides must still be exactly what `border` gave them.

```
border-top-width   4px        ← the longhand, written last
border-top-style   dashed     ← the side shorthand
border-top-color   (reset)    ← `border-top` stands for all three kinds, so the
                                colour it did not name went back to its initial
border-right-*     1px / solid / #111111
```

This is 9.15's ownership move stated as observable behaviour: if the box pass and the border pass had both materialised these longhands, one side would silently disagree — and it would be a *last-writer-wins* disagreement that depended on pass order, not a crash.

## 7. The pipeline, from one end to the other

`css.conformance.pipeline.tokenizer_to_computed` takes a single document and asserts at every stage: the tokenizer's tokens tile the source and include exactly one comment, one hash and two dimensions; the parser's rule has two declarations and a prelude that is component values; the selector grammar's `div.card` is `(0, 1, 1)`; the cascade's winner for `color` is `#336699`; the computed style has a colour and four distributed margins.

The claim being made is that the stages **compose**. Testing them separately — which is what the other forty-one suites do, thoroughly — is exactly how a mismatch *between* two of them goes unnoticed, because each stage is only ever asked with input the test itself wrote.

The prelude assertion is the boundary worth reading twice:

```
div.card  →  "div,.,card"
```

An ident, a delim and an ident. That is 9.3's boundary exactly: the grammar stores what the tokens *were*, and the selector parser is what gives them meaning. A build that had quietly parsed the prelude during parsing would fail here.

## 8. The malformed corpus

42 documents that are wrong in ways a single token never is: a rule whose prelude is not a selector, a declaration whose value is empty, a custom property that references itself, `revert`, `!important !important`, an unterminated comment, a string broken by a newline, two bytes of a three-byte UTF-8 sequence, a control byte used as a property name's first character, `:not(:not(:not(div)))`, `@media` and `@import` and `@font-face`, three hundred unbalanced braces.

Each goes through the entire chain — parse, add as a sheet, selectors, cascade, computed style — and the assertions are the four a pipeline owes:

1. every door that promises `RoboOk` keeps that promise;
2. the style exists and something can be read out of it, including through the query door;
3. the counters are self-consistent (`incomplete_count` never exceeds the entry count, and `has_invalid` agrees with it);
4. the **allocator returns to its baseline**, for every one of the 42 inputs.

The fourth is not decoration: a malformed document that leaked would be a malformed document that eventually exhausted memory. And the assertion the whole corpus exists for — that the process is still alive to make it — is the one whose failure mode is a dead test binary.

## 9. Determinism, over whole styles

Per-family suites assert determinism of one value. This compares two independent contexts by `CssComputedValueEquals` — **structurally**, not by text — for every entry, and checks the *specified* half is present on both. A difference in formatting cannot hide a difference in value, or the reverse.

## 10. The finding this phase was for

`css.capabilities` walks the capability struct up to `positioning` and then asserts the two flags that are `0`. **It never mentions `backgrounds` or `borders`.** Both were recorded by their own phases as having flipped to 1, and neither was asserted anywhere in the build:

```bash
$ grep -rho "caps\.backgrounds\|caps\.borders" css/tests/ Tests/
$ echo $?
1
```

A documented promise with no test behind it is a promise that can stop being true silently, which is exactly what a conformance phase exists to find. `css.conformance.capabilities.every_flag` now walks the struct **field by field** — all 27 — with the list written out rather than derived, so a field added later is a deliberate act here and a *missing* one shows as a flag nothing asserts. The struct's size is pinned against the list's length for the same reason:

```c
RoboCheckEqInt((int)(sizeof(flags) / sizeof(flags[0])),
                  (int)(sizeof(CssCapabilities) / sizeof(int)));
```

## 11. What the phase found

Six things. The first four are findings about the **product**, which is what makes them worth recording separately from the per-phase accounts.

1. **Two capability flags were asserted nowhere.** `backgrounds` and `borders`, both documented by their phases as 1, both untested. §10 is how they are now covered.
2. **No test had ever *computed* a registry initial value.** `css.registry.initial_values` asserts the 9.1 shape, and every per-family suite computes its own family's initials — but the *whole table* through the normaliser was never run. A row whose shape and initial had drifted would have computed `INVALID` on every element of every document, silently.
3. **A preserved shorthand and a colour spell `currentcolor` differently.** Five rows say `currentcolor` because they preserve the registry's tokens; six say `currentColor` because the colour module owns its own rendering. Found by a table diff, and now asserted in both directions.
4. **Inherited and non-inherited properties differ in whether they have an entry at all** — not in what the entry contains. A test that expected `<absent>` for an undeclared `letter-spacing` was asserting the intuitive rule rather than the real one, and the real one is the more useful statement.
5. **Five refusal messages were more specific than a summary would have guessed** — `not a padding length`, `not a background repeat value`, `unknown colour name`, `not a whole number`, `not a border style value`. A phase that had asserted its own paraphrase would have been asserting the wrong contract.
6. **The inline seam is per context, not per element.** The test double answers the same `style` attribute text for every element it is asked about, so a document carrying an inline `color` cannot also be the document in which the author's `color` rules are compared. The cascade test is built around that: the attribute names `margin-top`, which is where the *level* contest is, and every `color` assertion is about the author's own rules.

Three of the six were found by watching a test fail and reading the failure rather than by looking for a gap — which is the ordinary way, and the reason the phase's assertions were written to be *exact* rather than tolerant.

## 12. Running the suite

```bash
./scripts/robo test-css                                        # 609 tests
./Out/RoboTest.host.exe --filter css.conformance                # this phase (14 cases)
```

`docs/Tests.md` says what each asserts, and its §20 lists what this phase found.

## 13. The two host-window cases, and why they are not this subsystem's

Running the *whole* suite — the sanitized binary in particular, and from a shell Windows does not consider entitled to the foreground — can report `window.host.keyboard` and `window.host.mouse`, which assert `WindowRequestFocus(id) == RoboOk` and get `RoboNotSupported` (7) when `SetForegroundWindow` is declined. Both are environment-dependent: in this phase's runs the plain suite passed 2016/2016 once and reported the pair once, and the sanitized run reported the pair. No `window.host.*` case reaches any RoboCSS code, and no sanitizer diagnostic accompanied either failure.

## 14. What 9.16 does not do

- **It adds no production code.** Not one line of `Source/RoboCSS/` changed. A conformance phase that had to change the thing it tests would be reporting a defect, not a conformance result.
- **It is not a conformance suite against the CSS specifications.** Every assertion here is about *this build* — the shapes it chose, the refusals it words, the flags it reports. Nothing compares a computed value to a value the W3C test suite would expect, and `css.conformance.matrix.declarations` is a table of this build's contracts, not of the standard's.
- **It does not fuzz.** The 42-document corpus is written by hand and driven once each. There is no random generator, no coverage-guided search and no property-based shrinking — the runtime has no fuzzing framework, and the phase's instruction was not to introduce one for its own sake.
- **It does not test layout, paint or the DOM.** Those layers sit above RoboCSS; the real-DOM pipelines that exist are in `Tests/Suites/SuiteRoboCssDomSelect.c`, and this phase neither duplicates nor extends them.
- **`RoboCssApiFrozen` stays 0.** This phase adds no surface and freezes none; the version moves to 0.16.0 for the *promises*, not for the API. *(The version has moved twice since — 10.11's border family and 10.13's overflow family, which took it to 0.17.0 and grew these two tables from 49 to 63 and then to the 66 and 71 above.)*
