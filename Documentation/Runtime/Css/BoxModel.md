# RoboCSS Box Model (Phase 9.11)

A box is four edges, and CSS writes them twice:

```css
margin-top: 10px;  margin-right: 20px;  margin-bottom: 10px;  margin-left: 20px;
margin: 10px 20px;
```

Everything downstream — inheritance, computed values, and eventually layout — is
written against the **longhands**. So something has to turn the second spelling into
the first, and this phase is that something. What it deliberately is *not* is a
layout engine: no edge is measured, no margin collapses, and `50%` stays a percentage.

Its siblings are `docs/Colors.md` and `docs/Units.md`. Those two gave a *value* a
module; this one gives a **property family** one, which is a new kind of milestone in
Phase 9 — the first phase whose subject is a set of declarations rather than a
representation.

```
CSS text
   ↓  tokenizer (9.2)                `margin` → IDENT, `1px 2px` → DIMENSION, ws, DIMENSION
   ↓  grammar (9.3)                  declaration → component-value tree
   ↓  cascade (9.5) + specificity (9.6)
   ↓  inheritance (9.7)              ← the shorthands are distributed HERE
   ↓  RoboCSSBox                     the four sides, the three shorthands, the mapping
   ↓  computed values (9.8)          each longhand through its own value shape
   ↓  layout (a later phase)         the used width, the collapsed margin, the painted border
```

| Goal | What it delivers |
|---|---|
| 9.11.1 the sides | The four edges as an ordered, closed set — CSS's own order, because the mapping depends on it |
| 9.11.2 the shorthands | `margin`, `padding`, `border-width` and the longhand each side stands for, as one walkable table |
| 9.11.3 the mapping | The 1/2/3/4 component rule as four table rows rather than four branches per shorthand |
| 9.11.4 expansion | A shorthand value → four component values, with the two refusals stated |
| 9.11.5 the shapes | `EDGE_OFFSET`, `EDGE_INSET`, `LINE_WIDTH` — three value shapes that differ by vocabulary |
| 9.11.6 the registry rows | Fourteen properties: the twelve longhands, `border-width`, `box-sizing` |
| 9.11.7 resolution | Which declaration wins an edge, asked of the cascade and answered there |

## 1. The four sides

```c
typedef enum CssBoxSide {
    RoboCssBoxSideTop = 0,
    RoboCssBoxSideRight = 1,
    RoboCssBoxSideBottom = 2,
    RoboCssBoxSideLeft = 3
} CssBoxSide;
```

The **order** is load-bearing rather than conventional. `1px 2px` means "vertical,
then horizontal", and `1px 2px 3px` means "top, then both horizontal, then bottom" —
both statements are *about the order*. A set that listed the sides differently would
transpose every two- and three-component shorthand, which is why the suite walks the
enumerators in sequence and reads the names back rather than sampling one.

## 2. The shorthands

```c
typedef struct CssBoxShorthand {
    const char *name;
    size_t name_length;
    const char *longhands[RoboCssBoxSideCount];   /* indexed by side */
} CssBoxShorthand;
```

Three rows:

| Shorthand | top | right | bottom | left |
|---|---|---|---|---|
| `margin` | `margin-top` | `margin-right` | `margin-bottom` | `margin-left` |
| `padding` | `padding-top` | `padding-right` | `padding-bottom` | `padding-left` |
| `border-width` | `border-top-width` | `border-right-width` | `border-bottom-width` | `border-left-width` |

Anything else is reached by `CssBoxShorthandFind`, and a *longhand* is reached
by `CssBoxShorthandOfLonghand`, which answers the question a resolver actually
asks: "is this declaration one of four that have to be decided together?"

`border-width` is here although this build paints no border, and that is a decision
rather than an oversight. The four widths are the four values; a shorthand no longhand
ever saw would be a declaration no later phase could consume.

**`border` is deliberately not here.** It is a shorthand for four widths, four styles
and four colours, and this build represents one of those three. Expanding it would
invent two thirds of a declaration.

## 3. The mapping

```c
static const size_t box_component_map[4][4] = {
    /* 1: a a a a */ { 0u, 0u, 0u, 0u },
    /* 2: a b a b */ { 0u, 1u, 0u, 1u },
    /* 3: a b c b */ { 0u, 1u, 2u, 1u },
    /* 4: a b c d */ { 0u, 1u, 2u, 3u }
};
```

Rows are arities, columns are sides. Written as a switch it would be four branches
restated per shorthand; written as a table it is one row per arity, and a fifth arity is
a value this module refuses rather than extends.

The three-value row is the one worth naming: `top` differs from `bottom`, and `left`
borrows `right`. That is the row a hand-written branch gets wrong, and the suite asserts
all four outcomes for all four arities rather than one representative case.

## 4. Expansion, and the two refusals

```c
size_t CssBoxComponentCount(const CssValue *shorthand_value);
int    CssBoxIsExpandable(const CssValue *shorthand_value,
                                  size_t *out_component_count);
Result CssBoxExpandSide(const CssValue *shorthand_value,
                                       size_t component_count,
                                       CssBoxSide side,
                                       CssValue *out,
                                       CssDiag *diag);
Result CssBoxExpand(const CssValue *shorthand_value,
                                  size_t component_count,
                                  CssValue out_sides[4],
                                  CssDiag *diag);
```

Counting skips **trivia**: a declaration's value keeps its interior whitespace
(the grammar trims the ends, not the middle), so `1px 2px` arrives as three children and
is two components. A bare token — a hand-built value — counts as one, so both shapes
answer the same way.

Two things refuse to expand, and they refuse for different reasons:

- **Five or more components.** Not a longer shorthand, an *invalid declaration* that
  the grammar accepted because validity is not syntax.
- **A `var()` or `env()` reference.** Substitution happens *before* expansion in CSS,
  and substitution can change how many components there are: `--edges: 10px 20px` turns
  `margin: var(--edges)` into a two-value shorthand. Splitting it now would assign the
  whole reference to every side and be wrong on the first real declaration.

`calc()` needs no such care — `calc(10px + 5px)` is one component whatever is inside
it — and the suite pins both halves of that distinction from the same three-value input.

The result of expanding one side is a **list of one**, not the bare component, because
that is the shape a declared `margin-left: 10px` has. It is what lets a distributed
longhand go through the same normalisers as a written one with no special case.

`CssBoxExpand` is all-or-nothing: on any failure every side is left `KIND_NONE`,
so a caller that ignores the result cannot be handed two sides of a four-sided box.

## 5. The three value shapes

`auto` is a margin and is not a padding. `medium` is a border width and is not a margin.
`min-content` is a width and is neither. The families differ by **vocabulary**, and that
is why there are three shapes rather than one:

| Property family | Shape | Accepts |
|---|---|---|
| `margin-*` | `EDGE_OFFSET` | a length, a percentage, `auto` |
| `padding-*` | `EDGE_INSET` | a length, a percentage — **never** `auto` |
| `border-*-width` | `LINE_WIDTH` | a length, `thin` / `medium` / `thick` |

They share one body (`computed_compute_length_grammar`) and differ only in the grammar
they are handed — a keyword list, its length, and the sentence to use when an identifier
is not in it. That is what makes `padding: auto` an **INVALID** computed value instead of
a keyword a later phase would have to interpret, and it is asserted from both ends:
`auto` computes to a keyword under `EDGE_OFFSET` and to INVALID under `EDGE_INSET`.

Border-width keywords **stay keywords**. What `medium` is in pixels depends on the
border's style and the device, so resolving it here would be claiming a decision this
phase has not got the context to make — the same rule 9.10 applied to `2em`.

## 6. Fourteen registry rows

Twelve longhands, `border-width` as their third shorthand, and `box-sizing`:

| Row | Inheritance | Shape | Initial |
|---|---|---|---|
| `margin-top` … `margin-left` | non-inherited | `EDGE_OFFSET` | `0` |
| `padding-top` … `padding-left` | non-inherited | `EDGE_INSET` | `0` |
| `border-top-width` … `border-left-width` | non-inherited | `LINE_WIDTH` | `medium` |
| `border-width` | non-inherited | `PRESERVE` + `SHORTHAND` | `medium` |
| `box-sizing` | non-inherited | `KEYWORD` | `content-box` |

None of them inherits, and the suite asserts that from both directions: the parent's
`margin-left: 10px` and the child's absent `margin-left`, beside an inherited property
whose value *did* come across. That is what makes the absences a classification rather
than an empty result.

`box-sizing` is a value model with no layout behind it. The keyword is what a
declaration means here; which box a width applies to is a later phase's question, and
registering the property is what keeps the declaration alive until that phase arrives.

## 7. Expansion is not resolution

This is the phase's one architectural decision, and the rest follows from it.

Which declaration wins an edge is **the cascade's answer**, including when the two
declarations carry different property names:

```css
.box { margin: 10px; margin-left: 20px; }   /* the longhand is later, so it wins  */
.box { margin-left: 20px; margin: 10px; }   /* the shorthand is later, so it wins */
```

That is level, then specificity, then document order — the same three keys, asked across
two property names instead of within one — so it is asked of the cascade:

```c
int CssCascadeCandidateCompare(const CssCascade *cascade,
                                       const CssCascadeCandidate *a,
                                       const CssCascadeCandidate *b);
```

which answers through the same level table and the same specificity comparison the
resolution pass uses. **Nothing in the box model weighs a level or compares a
specificity**, and the suite pins the interaction from four directions: document order
both ways, specificity, origin, and `!important`.

Where it runs is `RoboCssInheritResolve`, as a fourth set between "what the element
declares" and "what the parent carries":

```
1   the properties the element's own cascade declares
1b  the box model's longhands, distributed out of their shorthands      ← here
2   the properties that inherit by default that the parent carries
3   the inherited-by-default defaults neither of the above supplied
```

Two consequences fall out of that placement rather than being added on top:

- **A distributed longhand is a declaration.** It goes through `inherit_value`, the same
  function a declared property goes through, so `margin: inherit` and `margin-left:
  inherit` mean the same thing to all four edges *by construction* rather than by two
  implementations agreeing. `initial`, `unset` and the carried `revert` follow for free.
- **The declared pass skips box-model longhands**, and the expansion pass materialises
  them — each edge exactly once, from whichever of the two candidates won. Without that
  split an edge would get two entries: one from its own declaration and one from the
  shorthand it lost to.

The shorthand's own entry is **kept**. The author's text is not destroyed on the way to
the four longhands, and a caller reading `padding` sees what was written.

## 8. When a shorthand cannot be distributed

The quiet failure, and the reason there is a counter:

```c
size_t CssInheritedStyleShorthandsUnexpanded(const CssInheritedStyle *style);
```

```css
#box { margin: 1px 2px 3px 4px 5px; padding: var(--edges) }
```

Both shorthands leave their own entry intact and produce **no** longhands. A consumer
reading the style would otherwise see a declaration and nothing underneath it and have no
way to tell that anything was left undone, so the resolution is marked `unsupported`,
the count is reported, and one debug line is logged — the same treatment a tokenizer
error gets. Eight edges are unanswerable, and the style says so.

## 9. Ownership

The module owns **nothing**. The shorthand table is `static const`; expansion copies
each component into a fresh tree the caller owns; the mapping is a table of integers.

A component's slices borrow the source that declared the shorthand, which is exactly the
rule every component value follows (`RoboCSSValue.h`) — so a value produced by expansion
is valid for as long as the sheet it came from, which is a lifetime the caller already
owns. The resolved style then deep-copies what it keeps, so a resolution outlives the
cascade it was resolved from.

`CssBoxExpandSide` disposes `*out` before it builds anything, and every failure
path leaves it `KIND_NONE` rather than half-built.

## 10. What 9.11 does not do

Deliberately absent, and each is a boundary rather than an unfinished edge:

- **Layout.** No box is measured, no margin collapses, no containing block is
  consulted. `width: 50%` is a percentage; `margin: auto` is the keyword.
- **Used values.** `border-width: medium` stays the keyword, because its pixel count is
  a used value.
- **Borders.** `border-width` is distributed; nothing is painted, and no border
  *style* or *colour* property is registered.
- **`border`, `background`, `font` and the other shorthands** whose longhands this
  build does not all represent.
- **`box-sizing` behaviour.** The value model exists; the arithmetic does not.

## 11. Running the suite

```bash
./scripts/robo test-css                      # the whole RoboCSS surface
./Out/RoboTest.host.exe --filter css.box     # this phase
./Out/RoboTest.host.exe --filter css.dom.box.pipeline   # over the real DOM
```

Thirteen cases in `css/tests/SuiteRoboCssBox.c` and one in
`Tests/Suites/SuiteRoboCssDomSelect.c`. `docs/Tests.md` says what each asserts;
§13 there lists the defects the suites across Phase 9 have found.
