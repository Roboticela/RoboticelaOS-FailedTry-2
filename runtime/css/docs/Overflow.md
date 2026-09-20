# RoboCSS Overflow — the `overflow` family

Module version at this record: **0.17.0**. The family was added by layout phase
**10.13 — Overflow**, the third time a *layout* phase has widened this module's
registry rather than a 9.x value phase doing it (9.15's borders was the second, and
10.11 consumed it).

The layout side of the phase is `layout/docs/Overflow.md`; this page is the CSS side —
what a value is, what is refused, and what a consumer reads.

---

## 1. Why this is a different kind of value

Every property before it is about the **box**: how big, how spaced, what colour. `overflow`
is about the **room the box has** — what happens to contents that do not fit inside the
box an author gave them — and two facts about it decide the whole shape here.

### The value is not per axis; the *property* is

CSS writes `overflow` as a shorthand over `overflow-x` and `overflow-y`, and the two
longhands are what everything downstream reads: a box can clip horizontally and not
vertically, and a build that collapsed the pair into one value could not express the
document that does. So the vocabulary is one **value**, and the axis belongs to the
**property**:

```c
ROBO_CSS_OVERFLOW_VISIBLE = 0   /* the initial value, so a cleared record holds it */
ROBO_CSS_OVERFLOW_HIDDEN  = 1
```

`overflow-x: hidden` and `overflow-y: hidden` are two typed values of that one value
set, and a consumer reads them one axis at a time — which is why
`robo_layout_overflow_edges_t` carries **two** modes and never one collapsed mode.

`VISIBLE` is **0** because it is the property's initial value: a cleared record holds
the state an author who declared nothing finds their box in.

### The shorthand is *read*, not expanded

No phase in this runtime expands a shorthand. `margin`, `padding`, `border` and
`background` are registered as shorthands and **carry their tokens** while their
longhands are what anything reads, and `overflow` could have taken the same road.

It deliberately does not. `overflow: hidden` is a declaration an author writes on nearly
every box they want clipped, and a `PRESERVED` row would make it a declaration *nothing
reads* — a document that says "clip this" laying out as if nobody had written anything.
So the shorthand's **own name** is registered on this vocabulary too, and a consumer that
finds no longhand reads it as speaking for **both** axes, which is what the shorthand
means one step earlier than CSS computes it.

The cost is stated where it is paid: `overflow: hidden visible` is legal CSS this build
**cannot read**, because reading it needs the expansion this registration exists to work
around. A declaration written that way is an invalid value, not a half-understood one.

## 2. The set is closed

Like `display`'s, `position`'s and `border-style`'s, and for the same reason: a layout
pass *dispatches* on this value, so `overflow: banana` has to be a mistake rather than a
keyword carrying the text `banana` that a consuming phase silently fails to interpret.

| Value | Status |
|---|---|
| `visible` | 0, the initial value |
| `hidden` | 1, a clipping boundary |
| `scroll` | named, **not** a row this build has — scrolling |
| `auto` | named, **not** a row this build has — scrolling |
| `clip` | named, **not** a row this build has — a rule this build's single boundary does not distinguish from `hidden` |

Each deferred value is named so the phase that adds it knows where it goes.

## 3. The three registry rows

| Property | Initial | Inherits | Value shape |
|---|---|---|---|
| `overflow` | `visible` | no | this vocabulary — the one shorthand in the table that carries a **typed value** rather than `PRESERVED` tokens |
| `overflow-x` | `visible` | no | this vocabulary |
| `overflow-y` | `visible` | no | this vocabulary |

They took the registry from **63** rows to **66** (50 of them non-inherited, from 47), and
the conformance tables with it: the initial-value table from 63 to 66 rows and the
declaration matrix from 66 to 71.

The rows are non-inherited for the reason a border's are: the room a box has is a fact
about that box.

## 4. What is refused

| Declaration | Outcome |
|---|---|
| `overflow-x: hidden` | a value of this vocabulary |
| `overflow-y: visible` | a value of this vocabulary |
| `overflow: hidden` | a value of this vocabulary, read for both axes |
| `overflow: banana` | `INVALID` — not an overflow value |
| `overflow: 20px` | `INVALID` — not an identifier |
| `overflow: hidden visible` | `INVALID` — the two-value form needs the shorthand expansion this registration exists to avoid |
| `overflow-x: var(--o)` | carried as an **unusable** declaration: it reaches layout as a declaration this build could not use, is used as `visible`, and is **counted** rather than dropped |

The last row is the one worth naming, and it is the layout side's rule: a declaration
this build cannot use is still a *declaration*, so a document whose clipping depends on a
`var()` reference is visible as such instead of silently laying out unclipped.

## 5. What a consumer reads

There is no geometry here. A value says *whether* an axis clips, and where the boundary
is belongs to the phase that has the box — so nothing in `RoboCSSOverflow.h` takes or
returns a rectangle, and the computed value carries no number at all.

```text
computed value kind   ROBO_CSS_COMPUTED_VALUE_OVERFLOW
read door             robo_css_computed_value_overflow(value)   -> visible for any other kind
capability flag       robo_css_capabilities_t.overflow == 1
```

The layout side reads the three properties through one seam and resolves them per axis,
longhands first and then the shorthand for whichever axis a longhand said nothing about;
`layout/docs/Overflow.md` §2–§5 is that account.

## 6. What is deliberately missing

- **expansion** of the shorthand, and therefore the two-value form (§1);
- **`scroll`, `auto` and `clip`** as values, and therefore anything a scrollport would
  mean (§2);
- **`overflow-anchor`, `overflow-clip-margin`, `overscroll-behavior`** — other rows, none
  registered;
- **any geometry.** A value is a mode per axis; the four extents a box's contents reached
  are a layout fact, recorded beside the box and measured against its padding box.
