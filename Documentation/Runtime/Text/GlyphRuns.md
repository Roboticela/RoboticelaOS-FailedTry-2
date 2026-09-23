# RoboText glyph runs (Phase 12.14)

The phase that turns a **glyph run** into the contract between shaping and everything above it. After 12.14 a
shaped text leaves RoboText as a `RoboTextGlyphRunList`: runs in logical order, a visual order beside them, and
per run the glyphs, positions, clusters, source range, font identity, script, direction, level, language,
metrics and the keys that make the set comparable.

Nothing above reads a font table, resolves a lookup, applies kerning or walks UTF-8. That is the phase's
purpose; the sections below are what it actually claims.

The authoritative text is `Runtime/Include/RoboText/RoboTextGlyphs.h` and
`Runtime/Include/RoboText/RoboTextRunList.h`; this document explains and fixes the vocabulary.

---

## 1. The pipeline

```
UTF-8 text
   ↓  RoboTextBufferView (Phase 12.4)
runs        script · direction · bidi level · font · language · features        (12.5–12.8)
   ↓  cmap
glyphs      one per scalar, with the face's own advance                         (12.4)
   ↓  GSUB                                                                     (12.10)
glyphs      substituted ids, ligatures, sources and components
   ↓  GPOS                                                                    (12.11)
glyphs      adjustments — placement offsets and advance deltas
   ↓  finalize                                                               (12.14)
**GlyphRun** glyphs · clusters · flags · metrics · totals
   ↓  list                                                                   (12.14)
**GlyphRunList** logical order · visual order · keys · validation
   ↓
measurement · layout · paint
```

There is no second positioning stage and no second reordering stage. Kerning is GPOS (12.13); the visual order
is Phase 12.8's answer, **read** by the list rather than recomputed.

---

## 2. Glyph identity

A glyph carries a **font-local glyph id** and never a code point. `ffi` shaped into one glyph is
`GlyphId 64257` in one font's numbering, and the same characters are different ids in another font.

Two fields keep the Unicode text reachable:

| Field | Meaning |
|---|---|
| `RoboTextGlyph::source` | the **scalar indices** that produced the glyph: `[a, b)`, never empty, an inverted range impossible |
| `RoboTextGlyph::codePoint` | the **character** at those scalars — and `0` when the source covers more than one |

`codePoint` is the field Phase 12.14 adds, and it exists because a glyph's *properties* are properties of the
character: whether it is a combining mark is the one this build reads. After substitution the text is no longer
in scope, so the character travels with the glyph instead. The two meanings of `0` — "several scalars" and "the
character really is U+0000" — are told apart by `source`'s own length, which is always present.

Never reinterpret a glyph id as Unicode, and never rebuild a character by concentrating on ids.

---

## 3. Position and advance semantics

```
origin          = pen + offset
after drawing   = pen += advance
```

| Field | Meaning |
|---|---|
| `advanceX` / `advanceY` | the device-pixel vectors the pen moves after this glyph |
| `offsetX` / `offsetY` | where the glyph sits relative to the pen |
| `advanceAdjustX` / `advanceAdjustY` | the **font's own** part of the advance, from GPOS |
| `offsetAdjustX` / `offsetAdjustY` | the font's own part of the offset |
| `positioned` | non-zero when GPOS wrote a value record for this glyph |

`advanceX == units→device(advanceUnits) + advanceAdjustX`, per glyph. That identity is asserted in
`text.glyphrun.totals`, and keeping the font's part beside the total is what lets a diagnostic say "the pen
moves 12.4px, of which −1.25 is the font's kerning" rather than guessing.

These are **final** values. No consumer recomputes them; `RoboTextGlyphRunAdvanceUpTo` is the prefix sum, so a
caret and a painter ask the same record.

---

## 4. Clusters

A cluster is a source range and a glyph range — the many-to-many mapping between the two index spaces, written
down once.

```
RoboTextGlyphCluster {
    RoboTextRange      source;      /* scalars, half-open, non-empty               */
    RoboTextGlyphIndex glyphStart;  /* first glyph, an index into the run's array  */
    RoboTextGlyphIndex glyphEnd;    /* one past the last                          */
}
```

All four shapes are representable:

| Shape | Example |
|---|---|
| 1 scalar → 1 glyph | `a` |
| 1 scalar → several glyphs | a decomposition |
| several scalars → 1 glyph | `ffi` → one ligature glyph; `e` + U+0301 → `é` when the face composes |
| several scalars → several glyphs | a base and a mark the face drew separately |

The grouping rule, and what it deliberately is **not**:

1. a glyph whose source *starts inside* the cluster's source continues it (a decomposition);
2. a glyph that is a **combining mark** continues it (a mark attaches to the base before it);
3. otherwise the glyph opens a new cluster.

Rule 2 is an approximation and is named as one: Unicode grapheme clusters (UAX #29) are not implemented, so
"a mark attaches left" stands in for a segmenter. It is correct for the scripts this build shapes, and a
grapheme-cluster goal extends `GlyphContinuesCluster` rather than adding a second grouping.

`RoboTextGlyph::cluster` is the **cluster index** a glyph belongs to — since 12.14, and not the identity
mapping it used to be. `RoboTextGlyphRunClusterForScalar` is the inverse door: a click gives a scalar index, and
this answers which cluster it is in.

**Clusters are in logical source order, always** (12.14.7). A right-to-left run's clusters are not reversed;
what makes the text read right-to-left is where the *run* is placed. Reversing this array is the single most
common way a bidi implementation destroys a source mapping.

---

## 5. Glyph flags

Six facts, and only the ones the grouping and a later line breaker need:

| Flag | Meaning |
|---|---|
| `ClusterStart` | the first glyph of its cluster |
| `ClusterContinuation` | a later glyph of the same cluster |
| `Mark` | a combining mark, by the character's `General_Category` being `Mn`, `Mc` or `Me` |
| `Ligature` | a glyph several scalars were folded into (`ligatureComponents >= 2`) |
| `UnsafeToBreak` | a line may **not** begin at this glyph |
| `UnsafeToConcat` | another run's glyphs may not be shaped against this glyph's boundary |

`control`, `mirrored`, `mapped`, `substituted` and `positioned` are **not** in the flag word: each is a fact
about a glyph's id, each already has its own field, and stating a fact twice is how two readings of one record
come to disagree.

`UnsafeToBreak` is set on a glyph that continues a cluster (breaking before it separates a mark from its base,
or a ligature's part from the rest of it) and on a glyph a font placed relative to its neighbour. It claims
nothing about the boundaries it leaves clear: line breaking needs script-specific rules this phase has no
vocabulary for. `UnsafeToConcat` is set on a ligature's product and on a font-positioned glyph — a contextual
lookup's context reaches across a boundary, so cutting a shape run there can change what the font resolves.

---

## 6. Metrics

A run carries the face's metrics at its own context, so line layout never asks the font parser again:

```
ascent · descent · lineGap · lineHeight · capHeight · xHeight · unitsPerEm · fromFace
```

They come from `RoboTextFontContextMetrics` — the same door the measurement uses, so a line box built from a
run's metrics and one built from the measurement agree by construction. `fromFace == 0` means a documented
estimate was used, which is **not** the same as a zero ascent: `RoboTextGlyphRunMetrics` answers the zeroed
record in that case, and the run's own `haveMetrics` flag is what tells the two apart.

A paragraph's metrics are its **first** run's. That choice is stated rather than hidden: a list whose runs
disagree about their metrics is a list whose lines do not line up, and a caller wanting a per-run answer reads
the run.

---

## 7. Totals

`RoboTextGlyphRunAdvance` is the sum of the glyphs' own `advanceX`, accumulated in `double` and rounded once.
`RoboTextGlyphRunAdvanceY` is the vertical total — zero for every run this build produces, and present because a
caller accumulating progress in a writing mode it was not given cannot ask which axis to read.

A combining mark is not an independent line advance in any special sense: the rule is simply that the total is
the sum of the **final** advances, so whatever GPOS did to a mark's advance is what the total contains. There is
no second summation rule for marks and none for ligatures — `ffi` is one glyph with one advance, and its
components' advances are not the shape's.

`painted width == measured width == the GlyphRun's advance`. Nothing above the glyph run re-derives it.

---

## 8. Logical order and visual order

```
the list          = logical order. Run i is the i-th in source. That index is its logical index.
RoboTextGlyphRunListVisualRun(list, v)   → the logical index to paint at visual position v
RoboTextGlyphRunListVisualIndex(list, l) → where logical run l sits visually
```

The visual order is a **permutation in a separate array**, never a mutation of the logical one. Both directions
are published because both are asked: a painter walks visual order, a caret maps a source position to a place on
the line and therefore goes logical → visual.

`RoboTextGlyphRunListOrderVisual(list, paragraph)` reads Phase 12.8's own answer — the `visualIndex` of the level
run each glyph run's source starts in — and sorts by it, breaking ties by logical index. It contains **no
bidirectional algorithm**: a second implementation of L2 in this file would drift from 12.8's the first time a
level resolution changed.

Two consequences worth stating:

* a glyph run that **straddles two level runs** is placed with the one its source *starts* in. A shaping
  boundary and a bidi boundary need not coincide, and when they do not, a run has one level by construction
  (its first scalar's, Phase 12.8);
* `RoboTextGlyphRunListVisualIsLogical` reports the order as the logical one for a single-run list, for a
  left-to-right paragraph, and for a list whose order has not been computed. Reported rather than inferred, so
  a caller checking the fact is not doing work.

`text.glyphrun.order_visual` is the evidence: `עברית English עברית` with the base declared right-to-left yields
a Latin run that is painted neither leftmost nor rightmost.

---

## 9. Run splitting

A run is cut wherever a property that affects shaping changes: font, script, direction, bidi level, language and
feature state. Most of that cutting is 12.5's and 12.13's — the run set already segments by script, resolves a
level per run, and cuts at a per-range feature override before any plan is resolved.

12.14 makes the *reason* readable:

```
RoboTextGlyphRunKey        what makes two runs different runs
RoboTextGlyphRunKeyEqual   whether they are the same shape run
RoboTextGlyphRunKeyDifference → which field differed, as a named enumerator
```

The key holds the font **context** (the borrowed face and the style it is set at), script, direction, level,
base direction, language, the feature set and the generation mode. It holds no source range: a range says where
a run is, the key says what it is, and two runs at different places in the text are the same shape run.

Naming the differing field is what makes splitting deterministic in a test: the suite asserts *why* two runs are
separate, not merely how many there are. `text.glyphrun.split` checks a script change and a feature override.

---

## 10. Run coalescing

Two adjacent runs merge when **both** hold:

* `RoboTextGlyphRunKeyEqual`, and
* `first->source.end == second->source.start`.

Neither is sufficient. Equal keys across a gap would merge two runs with text between them that the list does
not own; contiguous sources with different keys would merge a Latin run into an Arabic one.

`RoboTextGlyphRunListCoalesce` merges by **concatenation and re-finalization**: the merged run's clusters,
flags, totals and glyph indices are the concatenation's, derived by the same `RoboTextGlyphRunFinalize` that
generation ends with. A merge that added two totals and kept the first run's clusters would be a run whose own
validation fails, which is why this lives in the module rather than in a caller. After a merge the visual order
is reset to the logical one, because the run count changed and the old permutation names runs that no longer
exist.

A **view** (see §13) cannot merge its runs: a merge replaces a run and frees what it replaced, and a view does
not own its runs. It is refused, and `RoboTextGlyphRunListOwnsRuns` says which kind of list a caller holds.

---

## 11. Font fallback

One run is one selected font face. `Hello 世界` with a two-family chain is three runs — Latin, Han, Latin — and
each carries its own face, so a fallback boundary is visible as a key difference on the `font` field.

In this tree the shipped pack carries **one family**, so the Han run has the same face as the Latin runs and
counts its glyphs missing instead. `text.glyphrun.fallback` asserts exactly that: the runs are cut where the
*text* changes, the missing count follows the characters, and no run claims a face the caller did not give it. A
real multi-family chain is a **pack** change (assets + manifest + licence + image check), not a text-layer
change — the same limitation Phase 12.5 records.

---

## 12. Ligatures and kerning, seen from here

* A ligature is one glyph whose `source` covers several scalars and whose `ligatureComponents >= 2`, flagged
  `Ligature`, in one cluster covering all of them. The characters are untouched. The advance is the **resulting
  glyph's own** `hmtx` entry plus whatever GPOS did to it — never the sum of its components'.
* Kerning is a GPOS advance adjustment: `advanceAdjustX` is non-zero and `positioned` is set, `advanceX` is the
  total, and the measurement follows for free. There is no kerning-specific field, no `kerning` flag on a glyph
  and no code-path check anywhere above the feature set.

The order is mandatory and is a property of the pipeline, not of this layer: GSUB decides which glyphs there
are, GPOS decides where they sit, and GPOS sees the ligature rather than its components.

---

## 13. Ownership, immutability and lifetime

* `RoboTextGlyphRun` is **immutable** once generated: its only writer is `RoboTextGlyphRunFinalize`, which runs
  before the pointer is handed out. Every public door returns a `const` pointer.
* `RoboTextGlyphRunListCreate` **adopts** the runs it is given and releases them with the list.
* `RoboTextGlyphRunListCreateView` **borrows** them: a shape result builds a view over its own `glyphRuns`, so
  reading a result's shaped text never transfers ownership of it. `RoboTextShapeResultGlyphRunList` is that
  view, built once at the end of shaping and valid for as long as the result is.
* A later operation that needs a different shaped result **builds a new run**; it never edits one.

Thread safety follows from immutability: a run and a list are safe to read from several consumers at once
(layout, paint, diagnostics) with no mutable global shaping state — which is also why the script resolver and
the bidi resolver are stateless.

---

## 14. Validation

```
RoboTextGlyphRunValidate(run, &problem, diag)      → RoboOk | RoboInvalidArgument + the broken invariant
RoboTextGlyphRunListValidate(list, &problem, diag) → RoboOk | RoboInvalidArgument + the broken invariant
```

The run's invariants, all of them: glyphs and source agree about emptiness; every glyph's source lies inside the
run's; every advance and offset is finite; clusters tile the glyph array and their sources tile the run's in
ascending order without overlap; every glyph's `cluster` names the cluster containing it; the stored total is
the sum of the glyphs' own; every counter equals what the glyphs say; and the direction agrees with the parity
of the resolved level.

The list adds: runs in ascending source order, no two overlapping, every run's source inside the list's, the
visual order a permutation of `0..count-1`, and the total the sum of the runs'.

A **gap** between two runs is valid — a caller may hold the shaped runs of a text whose middle was shaped
elsewhere — while an **overlap** is refused, because two runs claiming one scalar is not a set of runs at all.
The same rule is enforced at construction, so a list's indices always mean one thing.

Validation is a door a test calls, not a check generation performs: a run this module produced is valid by
construction, and `text.glyphrun.validation` is where that is *proved* rather than believed.

---

## 15. The diagnostic block

`RoboTextGlyphRunWrite` and `RoboTextGlyphRunListWrite` render the record as text: the source range, script,
direction, level, mode, features, metrics with whether they came from the face, the totals, the per-flag counts,
then one line per glyph and one per cluster. Complete or absent, like every dump in this module: a block that
does not fit leaves the buffer empty, and a record that fails validation says `INVALID <problem>` on its first
line.

---

## 16. Caching

There is **no shaping cache**, and `RoboTextCapabilities::shapingCache` reports `0`.

The key one would use is here and is tested: `RoboTextGlyphRunKey` is exactly the tuple (font context, script,
direction, level, base direction, language, features, mode) that a cache would be keyed on, and
`RoboTextGlyphRunKeyDifference` is the comparison a hit test would make. The cache is absent because the phase's
own rule is that caching must never be required for correctness (12.14.33), and nothing in this tree has yet
measured a shaping workload where one pays. When one is added it is an optimization over these keys, with
invalidation on font data, pack, feature configuration and device scale — and a build without it must still
answer identically.

---

## 17. What this phase does not claim

* **Grapheme segmentation** (UAX #29). The cluster rule is "a mark attaches left" (§4), named as the
  approximation it is.
* **A shaping cache** (§16).
* **Line breaking.** `UnsafeToBreak` is information for Phase 19's line breaker, not a breaker.
* **Caret geometry, selection painting, editing, justification, hyphenation, letter spacing.** All later goals;
  the fields they will need (`cluster`, `source`, the prefix sum) are here so they do not have to reshape a run.

---

## 18. Tests

`Tests/Text/SuiteRoboTextGlyphRuns.c` — `text.glyphrun.*`, over the **shipped** pack through RoboFont alone:

| Case | What it proves |
|---|---|
| `key` | the key, its identity, and the named field that differs |
| `clusters` | the 1:1 mapping, the tiling, and the inverse door |
| `clusters_mark` | a base and a separately-drawn mark are one cluster, and the mark is unsafe to break at |
| `clusters_composed` | a face's composition is one glyph, one cluster, two scalars and no single character |
| `clusters_ligature` | `dlig` `!?` → interrobang: one glyph, one cluster, provenance intact, unsafe to concat |
| `flags` | the flag vocabulary, and that a word prints flags in the enum's order, never a prefix |
| `metrics` | the run's metrics are the context's, the line-box identity, and the empty case |
| `totals` | the total is the sum, the prefix sum, and the per-glyph advance identity |
| `order_logical` | a left-to-right list is its own visual order, and says so |
| `order_visual` | mixed Hebrew/Latin: the Latin word is painted neither leftmost nor rightmost |
| `split` | a script change and a feature override, each by the field that differs |
| `coalesce` | two halves of one word merge into a real run; script, gap and view are refused |
| `refusal` | an overlapping or descending set never becomes a list |
| `validation` | every run and list a result produces is valid, and the aggregate sums every counter |
| `fallback` | a script boundary without a fabricated font boundary |
| `write` | the block, and complete-or-absent |
| `performance` | the measured baseline (bytes, runs, glyphs, clusters, time) |

Boundary suites that must pass with it: `TestLayout` (measurement), `TestPaint` (glyphs, the pen, the raster),
`TestGraphics` (the glyph-generation seam), `TestFont` (the shipped face), `TestDesktop` (what the shell draws);
`TestDesktop -Target Qemu` when the guest image is involved.
