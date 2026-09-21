# RoboCSS Positioning (Phase 9.13)

`position` decides what an element's box is positioned *relative to*, and the five answers
are not five sizes of one behaviour:

```css
position: static;     /* not positioned at all — `top` is ignored */
position: relative;   /* in flow, then shifted from where flow put it */
position: absolute;   /* out of flow, placed against a positioned ancestor */
position: fixed;      /* out of flow, placed against the viewport */
position: sticky;     /* in flow until a scroll constraint moves it */
```

Modelling that as one identifier in one field is how `top: 10px` comes to move a `static`
element — the mistake CSS's own rule (`top` does not apply to `position: static`) exists to
prevent. So the vocabulary is a table, and each row carries the three facts the value *is*.

Its siblings are `docs/Display.md` (9.12 — the other closed vocabulary) and
`docs/BoxModel.md` (9.11 — the property family the four insets belong to). This phase is
the first with **two properties of different shapes told apart by survey**: a closed
vocabulary, and a number that must be an integer.

```
CSS text
   ↓  tokenizer (9.2)                `absolute` → IDENT, `-1` → NUMBER
   ↓  grammar (9.3)                  declaration → component-value tree
   ↓  cascade (9.5) + specificity (9.6)
   ↓  inheritance (9.7)              all six are non-inherited; `inherit`/`initial`/`unset` resolve here
   ↓  RoboCSSPosition                the five values, their flow and positioned halves
   ↓  computed values (9.8)          one ident → a typed POSITION value, or INVALID with a reason
   ↓  layout (a later phase)         the containing block, the offsets, the stacking order
```

| Goal | What it delivers |
|---|---|
| 9.13.1 the vocabulary | The five values as a closed set, each spelled by exactly one keyword |
| 9.13.2 flow and positioned | The two facts a position value *is*, in the same rows |
| 9.13.3 the `POSITION` shape | A value shape that accepts one identifier from the vocabulary and refuses the rest |
| 9.13.4 classification | `is_positioned`, `is_in_flow`, `is_out_of_flow` |
| 9.13.5 the four insets | `top`, `right`, `bottom`, `left` on the box model's `EDGE_OFFSET` shape |
| 9.13.6 `z-index` | `auto` or an integer, on a shape that refuses a fraction |
| 9.13.7 the registry rows | Six properties: non-inherited, with their standards initial values |

## 1. The five values

```c
typedef enum CssPositioning {
    RoboCssPositioningStatic = 0,
    RoboCssPositioningRelative = 1,
    RoboCssPositioningAbsolute = 2,
    RoboCssPositioningFixed = 3,
    RoboCssPositioningSticky = 4
} CssPositioning;
```

A closed set, and the enumerator values are **append-only**: a stored enumerator is a
contract, so `running()` joins as 5 rather than reshuffling what a previous phase wrote
down. `STATIC` is 0 and is a *value* — the initial one — which is why a cleared record
reads as `STATIC` for the same reason a cleared display record reads as `NONE`, and it is
guarded the same way.

Note the type's name: `CssPositioning`, not `CssPosition`. The latter is
already the **source position** type (offset, line, column), and the two are unrelated
enough that a shared spelling would be a misread waiting to happen. The enumerators keep
the `POSITIONING_` prefix and the functions the `positioning_` one for the same reason.

## 2. Flow and positioned

Each row carries two flags, and they are **not each other's opposite**:

| Value | in flow | positioned | Meaning |
|---|---|---|---|
| `static` | yes | no | takes up room; an offset does not apply |
| `relative` | yes | yes | takes up room; an offset shifts what is painted |
| `absolute` | no | yes | its place is taken by whoever comes next; offsets resolve against an ancestor |
| `fixed` | no | yes | as `absolute`, resolving against the viewport |
| `sticky` | yes | yes | takes up room until a scroll constraint moves it |

The second row of that table is the one that reads as a contradiction until you notice the
two questions are different. A `relative` box takes up room where flow put it *and* is a
containing block for an absolutely positioned descendant. A `sticky` box is the same, with
a scroll constraint on top.

They are two columns rather than one three-valued "kind" because they are asked separately
and by different stages: **flow** is the layout question (does this box take up room in its
parent), and **positioned** is the containment question (what does an offset inside this box
resolve against). Folding them into one kind would make every question a comparison against
a spelling.

```c
int CssPositioningIsPositioned(CssPositioning position);
int CssPositioningIsInFlow(CssPositioning position);
int CssPositioningIsOutOfFlow(CssPositioning position);
```

Every one is asked of the table, and `is_positioned` is deliberately **not** written as
`!= STATIC` even though the two agree today: `position: running()` is not positioned in this
sense, and the comparison would have to be revisited on the day it is a row.

`is_in_flow` and `is_out_of_flow` are separate functions rather than one negating the other.
An unknown enumerator answers 0 to **both**, because "this box is somewhere else" is a claim
a value nothing defined cannot support:

| Enumerator | `is_in_flow` | `is_out_of_flow` |
|---|---|---|
| `static`, `relative`, `sticky` | 1 | 0 |
| `absolute`, `fixed` | 0 | 1 |
| anything unrepresented (`(CssPositioning)99`) | 0 | 0 |

That third row is the module's one assertion that a shorter implementation would fail, and
the suite pins it.

## 3. Reading an identifier

```c
CssPositioning CssPositioningFromSlice(CssSlice keyword);
int                     CssPositioningSliceIsKnown(CssSlice keyword);
```

Matching is ASCII case-insensitive, as CSS spells every keyword, and against the canonical
spellings **only**: `ABSOLUTE` matches, `absolute!` does not, and `abs` does not either. An
unknown identifier answers `STATIC` rather than matching a prefix.

`STATIC` is both a value and the answer for "not one of them" — the one ambiguity this
interface has, and unlike the display vocabulary it **cannot** be resolved by a predicate on
the result, because `static` is the initial value and therefore the most likely thing a
caller is asking about. `from_slice("banana")` and `from_slice("static")` both answer
`STATIC`. So the question is asked of the **identifier**:

```c
if (!CssPositioningSliceIsKnown(text)) { refuse(text); }
else { use(CssPositioningFromSlice(text)); }
```

The computed-value shape asks both, in that order, and the order is load-bearing.

## 4. The `POSITION` shape

`RoboCssPropertyComputationPosition`, joined as the twelfth value shape:

| Written | Computed | Why |
|---|---|---|
| `static`, `relative`, `absolute`, `fixed`, `sticky` | POSITION › the value | one of the vocabulary |
| `ABSOLUTE` | POSITION › `absolute` | CSS folds keyword case |
| `running(foo)`, `centre`, `banana` | INVALID › *not a position value* | legal CSS this build has not added, or a word nothing knows |
| `20px`, `10`, `10%`, `#fff`, `"absolute"`, `url(absolute)` | INVALID › *not an identifier* | a token that is not an identifier at all |
| `absolute fixed`, `rgb(0, 0, 0)` | INVALID › *not a single position keyword* | more than one component, or one that is not one |
| `var(--p)`, `env(--p)` | PRESERVED, flagged unresolved | a reference, carried rather than refused |
| `nonsense(--p)` | INVALID › *not a single position keyword* | an unknown function is a mistake, not a reference |

The refusals stay three sentences, and the boundary between the last two rows is the one
worth naming: `var()` and `env()` are *references* — CSS whose value is decided where the
variable is — while `nonsense(--p)` is a function nothing defines. Carrying the first is the
correct answer; carrying the second would be reading a mistake as a deferral.

The `DISPLAY` and `POSITION` shapes **share one case body**
(`computed_compute_vocabulary`), because the vocabulary, its two doors and its refusals are
data in a row. A third closed vocabulary — `float`, `overflow`, `visibility` — is a row in
that table rather than a third copy of "ask the identifier, then ask the table".

## 5. The four insets have no vocabulary of their own

`top`, `right`, `bottom` and `left` are the four values `position` moves a box *by*, and
their grammar — `<length-percentage> | auto` — is the box model's `EDGE_OFFSET` shape
exactly, the one `margin-top` and its three relatives already use. This phase therefore
gives them registry rows and no grammar:

| Property | Shape | Initial | Inherited |
|---|---|---|---|
| `top` | `EDGE_OFFSET` | `auto` | no |
| `right` | `EDGE_OFFSET` | `auto` | no |
| `bottom` | `EDGE_OFFSET` | `auto` | no |
| `left` | `EDGE_OFFSET` | `auto` | no |

The reason is the same one that made 9.11 write the box edges as a table: a second shape
with the same grammar is how `top: 20%` and `margin-top: 20%` come to be normalised
differently. So `top: 25%` stays a percentage, `top: auto` stays the keyword, and
`top: min-content` is a mistake.

Their *names* are the side vocabulary's — `CssBoxSideName` spells `top`, `right`,
`bottom` and `left` in CSS's own order — so the four inset properties are the four sides.
The phase's suite walks the inset family over `robo_css_box_side_count()` and reads each
name from `CssBoxSideName` rather than hand-listing the four, because a suite that
typed them out would have no way to notice a registry row written against the wrong side.
`inset` as a shorthand would be one more row in that module's table, not anything new here.

## 6. `z-index`

`z-index` shares the `NUMBER` field with no integer type of its own:

```c
RoboCssPropertyComputationZIndex
```

| Written | Computed | Why |
|---|---|---|
| `auto` | KEYWORD › `auto` | not zero — "takes no part in its parent's stacking decisions" is a different statement from "at level zero" |
| `0`, `7`, `-3`, `+12` | NUMBER › the value, `is_integer` set | an integer |
| `1.5`, `0.0` | INVALID › *not an integer* | `0.0` is a number token, and accepting it would be the silent coercion the distinction exists to catch |
| `1e2` | INVALID › *not an integer* | a magnitude token, not an integer token |
| `red`, `10px`, `10%`, `#fff` | INVALID › *not a number* | not a number token at all |

The refusal of `0.0` is the one to read twice. `0.0` and `0` are the same magnitude and
**different tokens**, and CSS's `<integer>` grammar is about the token. Accepting `0.0` would
mean a build that could not tell a fractional magnitude from a whole one, which is exactly
the failure mode the `is_integer` flag exists to prevent — and it would be silent, because
the value would be right.

Nothing here is a stacking context: no number is compared, no order is computed, and `auto`
is never read as 0. The phase that builds a stacking context is the phase that reads this.

## 7. The registry rows

| Row | Inheritance | Shape | Initial |
|---|---|---|---|
| `position` | non-inherited | `POSITION` | `static` |
| `top`, `right`, `bottom`, `left` | non-inherited | `EDGE_OFFSET` | `auto` |
| `z-index` | non-inherited | `Z_INDEX` | `auto` |

`position` and `z-index` existed before this phase as generic `KEYWORD` rows; what changed
for `position` is its shape, and what `z-index` gained is a shape of its own where it had
the `KEYWORD` pass-through. Neither its classification nor its initial value moved. The
suite asserts the facts together — shape, non-inheritance, and that the initial value is a
string the vocabulary *knows* — because the initial text and the vocabulary are written in
different files and the failure would be quiet: a row whose initial text were not one of the
five would make `position: initial` resolve through a shape that refuses it.

## 8. Non-inheritance and the hole it leaves

All six properties are non-inherited, and that has the same consequence 9.12 documented for
`display`: 9.7 materialises initial values for the **inherited-by-default** properties only,
so an element that declares nothing has *no entry* for `position`:

```c
CssComputedStyleComputedByName(style, "position")   /* → NULL */
```

The answer still exists — the property's initial value — and the door that holds it is the
style context's:

```c
CssStyleContextQueryComputedValue(context, style, "position",
                                            &value, &from_initial, diag);
```

| Element | entry | `from_initial` | value |
|---|---|---|---|
| `#lifted { position: absolute }` | present | 0 | POSITION › `absolute` |
| an element that declares nothing | absent | 1 | POSITION › `static` |
| `#odd { position: banana }` | present, INVALID | 0 | INVALID |

`from_initial` is the field that carries the distinction, and a `NULL` style is the same
answer as an undeclared element rather than a failure. **This is why the phase adds no
accessor to the computed-style object**, for the same reason 9.12 added none: the container
cannot answer the common case, and a door that answers less than the one beside it is worse
than no door.

The third row is 9.8's contract rather than this phase's — a refused declaration stays in the
style saying it means nothing for the property, and `CssComputedStyleHasInvalid`
reports it. That matters here for a reason it does not for `display`:

```css
#odd { position: banana }
#odd { position: absolute }
```

A browser discards the first declaration and uses the second. Reporting the refusal lets a
later phase do that; inventing `static` would hide it *and* would be a position nothing
chose.

## 9. Where positioning participates in the rest of the pipeline

Nothing about the cascade, specificity, inheritance or the CSS-wide keywords was changed for
these properties, and the suite pins each of them from the positioning side:

- **Cascade.** An id selector outranks a type selector, and `!important` outranks a later
  normal declaration, whichever way round the two are written. An inline `style` attribute
  reaches the positioning shapes through the same grammar as a sheet declaration.
- **`inherit`.** A child can be given its parent's position: `#outer { position: absolute }`
  with `#inherit { position: inherit }` computes to `absolute`, with provenance `inherited`.
- **`initial`.** The property's own initial value, *not* the parent's: `static`.
- **`unset`.** Behaves as `initial` **because the property is not inherited** — asserted
  separately from `initial`, since it is the assertion that would fail if the property were
  misclassified.

## 10. Ownership

The module owns **nothing**. The vocabulary is `static const`; every accessor borrows it;
`CssPositioningWrite` writes into the caller's buffer and refuses rather than
truncating, reporting the length it needed so a caller can size from the refusal.
`RoboCssPositioningTextMax` is 9 and the longest keyword is `absolute` at 8 bytes — a
capacity that *works* rather than the length of the longest word, which is the difference the
suite caught: a buffer of exactly 8 bytes refuses the two keywords that fill it.

A computed `POSITION` value owns no bytes at all — it is an enumerator in a record whose only
owned members are `text` and `preserved`, and the kind says which are live. That makes
`CssComputedValueEquals` on two position values an enumerator comparison.

## 11. What 9.13 does not do

Deliberately absent, and each is a boundary rather than an unfinished edge:

- **Layout.** No containing block is found, no offset is resolved, no rectangle exists.
  `position: absolute` is a fact a later layout phase acts on, not an action this one takes.
- **Stacking contexts.** `z-index` is a typed number and nothing more. No number is compared
  and no paint order is computed; `auto` is never read as `0`.
- **Sticky behaviour.** `sticky` is a value, with the same two facts as any other, and it
  classifies as in flow and positioned — which is exactly what it is before anything scrolls.
  What a sticky box does relative to a scrollport is a scrolling question. Refusing the value
  would be the other wrong answer: `position: sticky` is legal CSS an author has written.
- **`inset`, `inset-block`, `inset-inline`.** Shorthands this build has not distributed. The
  first would be one more row in `RoboCSSBox.h`'s shorthand table.
- **Percentage resolution.** `top: 25%` stays `25%`; it needs a containing block this build
  does not compute.
- **`position` in `@media`, on pseudo-elements, or as an animation target.** None of those
  mechanisms exists yet.

## 12. Running the suite

```bash
./scripts/robo test-css                          # the whole RoboCSS surface
./Out/RoboTest.host.exe --filter css.position    # this phase (19 cases)
./Out/RoboTest.host.exe --filter css.dom.position.pipeline   # over the real DOM
```

`docs/Tests.md` says what each asserts; §13 there lists the defects the suites across
Phase 9 have found.
