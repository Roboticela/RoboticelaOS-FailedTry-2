# Fragmentation vocabulary (Phase 19.7)

Version: `0.40.0`; capability: `CssCapabilities.fragmentation`.

This is the **CSS half** of Phase 19.7. It is a vocabulary and nothing else: five registered
rows, the two closed sets they are drawn from, and the classifications a fragmentation pass
dispatches on. Where the splits fall is `Runtime/Source/RoboLayout/RoboLayoutFragment.c`; see
[Layout/Fragmentation.md](../Layout/Fragmentation.md).

## 1. The two closed sets

`break-before` and `break-after` share one set — CSS calls it `break-between` — and
`break-inside` has a smaller one of its own. That asymmetry is not a convenience: an interior
boundary has no next fragmentainer for a break to begin one, so a forced *inside* value is not
a value with no effect, it is not a value.

| Set | Values | Count |
|---|---|---|
| `break-between` (`RoboCSSFragment.h`) | `auto`, `avoid`, `avoid-page`, `avoid-column`, `avoid-region`, `page`, `column`, `region`, `always`, `left`, `right`, `recto`, `verso` | 13 |
| `break-inside` (`RoboCSSFragment.h`) | `auto`, `avoid`, `avoid-page`, `avoid-column`, `avoid-region` | 5 |

## 2. The four classifications, and the two that are deliberately independent

Each `break-between` row carries four columns rather than a spelling for a consumer to compare:

| Classification | `auto` | the four `avoid`s | `page` | `column` | `region` | `always` | `left`/`right`/`recto`/`verso` |
|---|---|---|---|---|---|---|---|
| `forced` | 0 | 0 | 1 | 1 | 1 | 1 | 1 |
| `avoided` | 0 | 1 | 0 | 0 | 0 | 0 | 0 |
| `columnBreak` | 0 | 0 | 0 | **1** | 0 | **1** | 0 |
| `pageBreak` | 0 | 0 | **1** | 0 | 0 | **1** | **1** |

`forced` and `avoided` are independent rather than one three-way switch: no row is both, `auto`
is neither, and the four `avoid` rows are avoided without being forced.

`region` is the row that makes `columnBreak` and `pageBreak` two columns instead of one — it is
**forced** and honoured by neither container this build lays out, so it earns a row that nothing
claims. `always` is the generic forced break and is the one value both columns set. Treating
"forced" as one thing is the mistake this table exists to prevent.

## 3. The five registered rows

| Property | Inherited | Initial | Computation | Why |
|---|---|---|---|---|
| `break-before` | no | `auto` | `BreakBefore` | a break belongs to the box whose boundary it is |
| `break-after` | no | `auto` | `BreakAfter` | the same vocabulary on the trailing edge — a second *computation*, one table |
| `break-inside` | no | `auto` | `BreakInside` | the interior set |
| `orphans` | **yes** | `2` | `Orphans` | a positive whole number; a property of the text, not of a boundary |
| `widows` | **yes** | `2` | `Widows` | the mirror of `orphans` |

`break-before` and `break-after` are **two computations and one vocabulary**, which is the
reverse of the pair `orphans`/`widows` — two properties, one shared *shape*. The registry keys a
computation by property, so neither pair can be collapsed; what differs is which half is shared.

`orphans` and `widows` are the family's only inherited rows. A break describes a boundary and
belongs to the box that owns it, so a child inheriting one would force a break it never asked
for; a line floor describes how much text a fragment may be left with — a property of the
*text* — so a nested block keeps the page's floors.

`orphans: 0` is **refused** rather than clamped. "Keep no lines" is not a smaller floor, it is the
absence of one, and CSS says the property must be positive. A fraction is refused too: a line is
countable.

## 4. Not registered, and named rather than implied

* **The legacy `page-break-before` / `page-break-after` / `page-break-inside` spellings.** CSS
  still accepts them as aliases of the three `break-*` properties. An alias is a registry row and
  this build has none, so a stylesheet using them gets no value rather than a silently different
  one.
* **`box-decoration-break`.** It describes how a *fragment's* edges are drawn, which is a paint
  question and a fragment-geometry question neither of which exists here.
* **A fragmentation of anything.** No function in this family splits a box, sizes a fragmentainer,
  counts lines on either side of a break or balances a column. A value says *where* a split is
  allowed or required.

## 5. Tests

`Tests/Css/SuiteRoboCssFragment.c` — seven cases, domain `css.fragment.*`:

| Case | Asks |
|---|---|
| `between_vocabulary` | the thirteen words, the round trip, and the absent/unknown answers |
| `between_classifications` | all four columns row by row, plus `region`'s stated exception and `forced && avoided == 0` |
| `inside_vocabulary` | the five words, and the set boundary — `break-inside: column` is not a value |
| `computed` | the declaration each property computes through, refusals included, and `orphans`/`widows`' bound |
| `registry` | the five rows, their inheritance, their computations and their initials |
| `capabilities` | the flag, the version, the computations' names and the domain |
| `determinism` | one declaration computes to the same value twice |
