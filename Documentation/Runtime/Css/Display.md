# RoboCSS Display (Phase 9.12)

`display` is the one property every later phase reads on every element, and it is not
a word:

```css
display: block;         /* a box is generated; it is block-level; its contents are in flow */
display: none;          /* no box is generated at all */
```

Those are not two sizes of the same answer. `block` says three things at once and `none`
says the first of them is *false* — which is why a build that modelled `display: none`
as zero geometry would be wrong in a way no test of a rectangle would catch.

Its siblings are `docs/Colors.md` and `docs/Units.md` (a *value* gets a module) and
`docs/BoxModel.md` (a *property family* gets one). This phase gives a **single property**
one, and the reason is the opposite of the box model's: a family got a module because it
had four longhands to distribute, and `display` gets one because it has a closed
vocabulary that has to be refused at the edges.

```
CSS text
   ↓  tokenizer (9.2)                `inline-block` → IDENT, `20px` → DIMENSION
   ↓  grammar (9.3)                  declaration → component-value tree
   ↓  cascade (9.5) + specificity (9.6)
   ↓  inheritance (9.7)              `display` is non-inherited; `inherit`/`initial`/`unset` resolve here
   ↓  RoboCSSDisplay                 the four values, their outer/inner halves, the classifications
   ↓  computed values (9.8)          one ident → a typed DISPLAY value, or INVALID with a reason
   ↓  layout (a later phase)         the box, its formatting context, its geometry
```

| Goal | What it delivers |
|---|---|
| 9.12.1 the vocabulary | The four values as a closed set, each spelled by exactly one keyword |
| 9.12.2 outer and inner | The two halves of a display value, as CSS Display 3 names them, in the same rows |
| 9.12.3 the shape | `DISPLAY` — a value shape that accepts one identifier and refuses the rest |
| 9.12.4 classification | `generates_box`, `is_none`, `is_block_level`, `is_inline_level`, `is_inline_block` |
| 9.12.5 the registry row | `display`: non-inherited, the `DISPLAY` shape, initial `inline` |
| 9.12.6 the query door | How a consumer asks an element for its display when it declared nothing |

## 1. The four values

```c
typedef enum CssDisplay {
    RoboCssDisplayNone = 0,
    RoboCssDisplayBlock = 1,
    RoboCssDisplayInline = 2,
    RoboCssDisplayInlineBlock = 3
} CssDisplay;
```

A closed set, and the enumerator values are **append-only**: a stored enumerator is a
contract, so `flex` joins as 4 rather than reshuffling what a previous phase wrote down.

The vocabulary is *closed* — and that is the phase's decision, not an unfinished list.
An unknown identifier is a mistake with a diagnostic:

```css
display: bogus;     /* INVALID: not a display value     */
display: 20px;      /* INVALID: not an identifier       */
display: inline block;  /* INVALID: not a single display keyword */
```

Every one of those was previously readable as a computed keyword carrying its own text,
because the generic `KEYWORD` shape accepts any identifier. That shape is right for a
property whose meaning belongs to a phase that has not landed (`cursor: grab`), and wrong
for the one property a layout pass keys on: `display: bogus` would arrive at that pass as
a value it could only fail on. So `display` moved off `KEYWORD` and onto a shape of its
own, and the difference between the two shapes is one branch — *ask the vocabulary*.

## 2. Outer and inner

A display value is three facts, and they are the three CSS Display 3 names for them:

| Value | outer | inner | Meaning |
|---|---|---|---|
| `none` | `NONE` | `NONE` | no box at all |
| `block` | `BLOCK` | `FLOW` | block-level, contents share their parent's context |
| `inline` | `INLINE` | `FLOW` | inline-level, contents share their parent's context |
| `inline-block` | `INLINE` | `FLOW_ROOT` | inline-level, contents in a formatting context of its own |

The inner column is not decoration, and `inline-block` is the row that earns it. CSS
defines `inline-block` as `inline flow-root`, and this table says the same thing: an
inline outer with a `flow-root` inner. A build that carried only "is it inline" would
need a second flag beside it — "…but also atomic" — and two facts about one row is how
they come to disagree. The predicate is derived from the two columns:

```c
int CssDisplayIsInlineBlock(CssDisplay display)
{
    return CssDisplayOuter(display) == RoboCssDisplayOuterInline &&
           CssDisplayInner(display) == RoboCssDisplayInnerFlowRoot;
}
```

The two columns in one row are where the *next* values arrive, and none of them is
claimed here:

| Future value | outer | inner |
|---|---|---|
| `flex`, `grid`, `table` | `BLOCK` | a new inner this build would define |
| `inline-flex`, `inline-grid`, `inline-table` | `INLINE` | likewise |
| `flow-root` | `BLOCK` | `FLOW_ROOT` |
| `contents` | `NONE` — and **not** the value `none` | `NONE` |

`contents` is why the two classification questions are two functions. It generates no box,
like `none`, and its children are still generated and still laid out in the place it
would have occupied — so "is there a box" and "was this written as `none`" are different
questions:

```c
int CssDisplayGeneratesBox(CssDisplay display);   /* asked of the outer column */
int CssDisplayIsNone(CssDisplay display);         /* asked of the value */
```

They agree for all four values today. The suite asserts that they agree *and* that
exactly one value generates no box, so the day they diverge is the day a row was added
and the counts move.

## 3. Reading an identifier

```c
CssDisplay CssDisplayFromSlice(CssSlice keyword);
int                CssDisplaySliceIsKnown(CssSlice keyword);
```

Matching is ASCII case-insensitive, as CSS spells every keyword, and against the
canonical spellings **only**: `inline-block` matches, `inline_block` does not, and
`inlineblock` does not either. An unknown identifier answers `NONE` rather than matching
a prefix.

`NONE` is both a value and the answer for "not one of them", so the result alone cannot
say whether an identifier was recognised — `CssDisplayFromSlice("bogus")` and
`CssDisplayFromSlice("none")` both answer `NONE`. The unit vocabulary has the
same shape of ambiguity and resolves it with `CssUnitIsKnown`, which works there
because `RoboCssUnitNone` is *not* a unit. Here it is a display value, so the
question has to be asked of the **identifier**:

```c
if (!CssDisplaySliceIsKnown(text)) { refuse(text); }
else { use(CssDisplayFromSlice(text)); }
```

That ordering is load-bearing, and it is what the computed-value shape does — a check on
the *result* would accept `display: bogus` as `display: none`, which is the single most
damaging confusion this property has.

## 4. The shape

`RoboCssPropertyComputationDisplay`, joined as the eighth value shape:

| Written | Computed | Why |
|---|---|---|
| `none` | DISPLAY › `none` | one of the vocabulary |
| `block` | DISPLAY › `block` | one of the vocabulary |
| `inline` | DISPLAY › `inline` | one of the vocabulary |
| `INLINE-BLOCK` | DISPLAY › `inline-block` | CSS folds keyword case |
| `flex`, `grid`, `table`, `contents`, `inline-flex` | INVALID › *not a display value* | legal CSS this build has not added — a row, not a word it does not know |
| `bogus`, `red` | INVALID › *not a display value* | an identifier outside the vocabulary |
| `20px`, `10`, `10%`, `#fff`, `"block"`, `url(block)` | INVALID › *not an identifier* | a token that is not an identifier at all |
| `inline block`, `block flow`, `rgb(0, 0, 0)` | INVALID › *not a single display keyword* | more than one component, or one that is not one |
| `var(--d)` | PRESERVED, flagged unresolved | a reference, carried rather than refused |

The refusals stay four sentences because they are four different things to tell an
author, and because `flex` deserves a better answer than `bogus` — the first is a value
this build has not added, the second is a word nothing knows. `rgb(0, 0, 0)` lands in the
third bucket rather than the second: it is one component and it is not an identifier, and
"not a single display keyword" names that without claiming anything about the colour.

`display: var(--d)` is **carried**, with the unresolved flag set — the same answer 9.9
gives a colour space this build cannot represent. It is legal CSS whose value is decided
where the variable is, and neither `inline` nor a refusal would be that value.

## 5. What is deliberately not parsed

**The multi-keyword syntax.** `display: inline flex` and `display: inline-block flow-root`
are legal CSS 3 and are *refused* here, with a diagnostic that says why. This is a
boundary rather than an unfinished edge: the two-keyword grammar is a grammar, the
vocabulary's outer/inner columns are where it arrives, and until then carrying the tokens
would put a value nothing can read into the one property a layout pass keys on. CSS's own
recovery rule for a declaration the parser cannot use is to drop it, and dropping is what
happens.

**HTML's defaults.** A `div` is block and a `span` is inline, and neither fact is
anywhere in this module — they are a user-agent *stylesheet*, and a vocabulary is not a
stylesheet. A `div` with no declaration computes the property's initial value `inline`
like anything else, and the phase that ships user-agent styles is the phase where a `div`
becomes block. The suite asserts this deliberately: `h1`, `div` and `span` all answer
`inline` when nothing declares them.

**No element is removed from anything.** A computed `display: none` is a fact a later
layout phase acts on, not an action this phase takes. There is no tree to remove an
element from yet, and inventing zero geometry would be inventing the answer.

## 6. The registry row

| Row | Inheritance | Shape | Initial |
|---|---|---|---|
| `display` | non-inherited | `DISPLAY` | `inline` |

The row existed before this phase; what changed is its shape. Its classification and its
initial value are untouched, and the suite asserts the three facts together — the shape,
the non-inheritance, and that the initial value is a string the vocabulary *knows*:

```c
RoboCheckEqInt(definition->computation, RoboCssPropertyComputationDisplay);
RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
RoboCheckStrEq(definition->initial_text, "inline");
RoboCheckEqInt(CssDisplayFromText(definition->initial_text),
                  RoboCssDisplayInline);
```

The initial value is checked *as text the vocabulary knows* rather than as a string,
because the two are written in different files and the failure would be quiet: a row
whose initial text were not one of the four would make `display: initial` resolve through
a shape that refuses it.

## 7. Non-inheritance, and the hole it leaves

`display` does not inherit, and that has a consequence a consumer has to know about.

9.7 materialises initial values for the **inherited-by-default** properties only. So an
element that declares nothing has *no entry* for `display`:

```c
CssComputedStyleComputedByName(style, "display")   /* → NULL */
```

That is the right shape for a container and a poor one for a question. "What is this
element's `display`" has an answer — the property's initial value — and it is a fact only
something holding the registry can state. The door that holds one is the style context's,
and it was already there:

```c
CssStyleContextQueryComputedValue(context, style, "display",
                                            &value, &from_initial, diag);
```

| Element | entry | `from_initial` | value |
|---|---|---|---|
| `#shown { display: inline-block }` | present | 0 | DISPLAY › `inline-block` |
| an element that declares nothing | absent | 1 | DISPLAY › `inline` |
| `#odd { display: bogus }` | present, INVALID | 0 | INVALID |

`from_initial` is the field that carries the distinction, and a `NULL` style — "nothing
was computed for this element" — is the same answer as an undeclared element rather than
a failure. **This is why the phase adds no accessor to the computed-style object**: the
container cannot answer the common case, and a door that answers less than one beside it
is worse than no door.

The third row is the phase's one quiet failure, and it is 9.8's contract rather than this
phase's: a refused declaration stays in the style, saying that it means nothing for the
property, and `CssComputedStyleHasInvalid` reports it. Answering `inline` there
would be a guess in the one place the pipeline deliberately does not guess, and it would
also be *wrong* in a way nothing could see:

```css
#odd { display: bogus }
#odd { display: block }
```

A browser discards the first declaration and uses the second, so the answer is `block`.
Reporting the refusal lets a later phase do that; inventing `inline` would hide it.

## 8. Where `display` participates in the rest of the pipeline

Nothing about the cascade, specificity, inheritance or the CSS-wide keywords was changed
for this property, and the suite pins each of them from the display side:

- **Cascade.** An id selector outranks a type selector, and `!important` outranks a later
  normal declaration, whichever way round the two are written. An inline `style`
  attribute reaches the display shape through the same grammar as a sheet declaration.
- **`inherit`.** A child can be given its parent's display: `#outer { display: block }`
  with `#inherit { display: inherit }` computes to `block`, with provenance `inherited`.
- **`initial`.** The property's own initial value, *not* the parent's: `inline`.
- **`unset`.** Behaves as `initial` **because the property is not inherited** — this is
  the assertion that would fail if `display` were misclassified, which is why it is
  asserted separately from `initial`.

## 9. Classifications

```c
int CssDisplayGeneratesBox(CssDisplay display);
int CssDisplayIsNone(CssDisplay display);
int CssDisplayIsBlockLevel(CssDisplay display);
int CssDisplayIsInlineLevel(CssDisplay display);
int CssDisplayIsInlineBlock(CssDisplay display);
```

Every one is asked of the table. There is no layout algorithm behind any of them, and the
suite asserts the invariants that keep the table honest as rows are added: block-level and
inline-level are mutually exclusive and together account for exactly the values that
generate a box; a boxless value has no inner display; an atomic value is inline-level.

An unknown enumerator answers `NONE`/0 to every one of them rather than the first row's
answers, which is the same rule the unit vocabulary follows.

## 10. Ownership

The module owns **nothing**. The vocabulary is `static const`; every accessor borrows it;
`CssDisplayWrite` writes into the caller's buffer and refuses rather than
truncating, reporting the length it needed so a caller can size from the refusal.
`RoboCssDisplayTextMax` is 16 and `inline-block` is 12.

A computed DISPLAY value owns no bytes at all — it is an enumerator in a record whose
only owned members are `text` and `preserved`, and the kind says which are live. That
makes `CssComputedValueEquals` on two display values an enumerator comparison,
and a copied style has nothing to free.

## 11. What 9.12 does not do

Deliberately absent, and each is a boundary rather than an unfinished edge:

- **Layout.** No box is generated, no formatting context is established, no element
  leaves a tree. `display: none` is the *absence of a box*, recorded, not an action.
- **The multi-keyword syntax.** Refused, with the outer/inner columns as the extension
  point.
- **`flex`, `grid`, `table`, `list-item`, `contents`, `flow-root`.** Rows this build has
  not added. `contents` will also be the first value where "generates a box" and "is
  `none`" differ.
- **User-agent styles.** A `div` is not block, because that is a stylesheet and this is a
  vocabulary.
- **`display` on pseudo-elements, in `@media`, or as an animation target.** None of those
  mechanisms exists yet.

## 12. Running the suite

```bash
./scripts/robo test-css                       # the whole RoboCSS surface
./Out/RoboTest.host.exe --filter css.display  # this phase (16 cases)
./Out/RoboTest.host.exe --filter css.dom.display.pipeline   # over the real DOM
```

`docs/Tests.md` says what each asserts; §13 there lists the defects the suites across
Phase 9 have found.
