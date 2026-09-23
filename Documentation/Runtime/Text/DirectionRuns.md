# RoboText direction runs (Phase 12.7)

The direction stage answers one question per scalar of a text:

> Which **logical directional context** does this scalar belong to?

and it answers it without running the Unicode Bidirectional Algorithm. That distinction is the phase, and
it is worth stating before anything else:

```text
12.7 (here)   a scalar's Bidi_Class, the paragraph direction a context names, and the neutral/number
              rules that combine them into a direction per run.
12.8          embedding levels, the explicit formatting characters' *contents*, weak-rule refinement
              (W2/W4/W5/W7 in full), isolates, reordering (L1/L2) and visual order.
```

So a direction run says *"these scalars are strong RTL"* or *"these scalars are a number"* — never
*"these scalars are at level 1 and must be painted in this visual order"*. **Source text is never
reversed, reordered or copied by this stage.** `RoboTextCapabilities.bidiAlgorithm` is `0`, and that flag
is the honest record of the boundary.

Owner: `Runtime/Source/RoboText/RoboTextDirection.c` (the resolver, the class projection and the
diagnostics). Public contract: `Runtime/Include/RoboText/RoboTextDirection.h`; the vocabulary
(`RoboTextDirectionClass`, `RoboTextDirectionMode`, `RoboTextDirectionContext`, `RoboTextDirectionRun`)
is in `Runtime/Include/RoboText/RoboTextTypes.h`. The type is
`RoboTextDirectionRun { RoboTextRange range; RoboTextDirection direction; }` — a source range and a
direction, and nothing else: no glyph, no position, no font, no paint.

---

## 1. The pipeline

```text
UTF-8 → TextBuffer → decoder → Bidi_Class per scalar
                                        │
                            RoboTextDirectionClassOf  (strong / number / mark / neutral)
                                        │
   RoboTextDirectionContext ────────────┤  (mode: explicit ltr, explicit rtl, or auto = P2/P3)
   RoboTextDirectionResolveParagraph ───┘  (the paragraph direction, resolved once)
                                        │
                                        ▼
                       RoboTextDirectionRunsResolve   one forward pass
                                        │
                     ┌──────────────────┴──────────────────┐
                     ▼                                     ▼
        RoboTextDirectionRun[]                  RoboTextDirection per scalar
        (what a caller reads)                   (what the analysis runs are cut by)
```

`RoboTextDirectionRunsResolve` fills both outputs in the **same pass**, which is what makes the run array
and the per-scalar array the same answer rather than two computations that could disagree.
`RoboTextAnalysis` keeps the run array it resolved (`RoboTextAnalysisDirectionRun`) and cuts its analysis
runs against the per-scalar array, and `RoboTextRunSet` keeps its own runs from the same call — one
resolver, three readers.

## 2. The rule

Applied in this order, and the code in `RoboTextDirection.c` is that order and nothing else:

1. **A strong scalar keeps its own direction.** `L` is left-to-right; `R` and `AL` are right-to-left.
   These are the only scalars that have a direction of their own, and they are the only ones a declared
   paragraph direction cannot move.
2. **A nonspacing mark takes the direction of the character before it** — UAX #9's W1. This is why a base
   character and its accent can never be split into two runs, and why a mark at the *start* of a text is
   neutral, which is W1's own answer for it.
3. **A block of number-class scalars with a digit in it is a number**, and a number is **left-to-right**
   whenever the paragraph direction is known. A block of those classes with *no* digit — a lone comma, a
   lone full stop — is punctuation, and falls through to rule 4.
4. **Everything else is one block of neutrals**, resolved against the nearest **committed unit** on each
   side: the sides agreeing gives that direction; the sides differing, or either side being empty, gives
   the paragraph direction.

Rules 3 and 4 are UAX #9's I1/I2 and N1 read at this stage's level, and two of their details are what make
the runs coincide with the algorithm's *level runs* rather than approximately coincide:

- **A number is left-to-right in either base.** In an even (left-to-right) embedding level an `EN` or an
  `AN` goes up *two* levels; in an odd one it goes up *one*. Its level is therefore even either way, and
  even is left-to-right. `"مرحبا 123"` is consequently `RTL`, `RTL` (the space) and `LTR` (the digits) —
  three runs, not one. An RTL paragraph is not a paragraph in which every character is RTL.
- **A number lends the *nearest strong scalar's* direction to the neutral beside it**, or the paragraph
  direction when it has none. That is W7's backward search spelled as a fact about influence rather than
  about the number's own type, and it is what makes `"123 456"` one left-to-right run in a left-to-right
  paragraph and three runs (`LTR`, `RTL`, `LTR`) in a right-to-left one.

## 3. Why a digit is not "left-to-right"

The phase's §10 forbids `digit → LTR`, and the code keeps that prohibition visible in three places:

| Fact | Where it is visible |
|---|---|
| `2` is `EN` and `٣` is `AN`, and neither is a strong class | `RoboTextDirectionClassOf('5') == RoboTextDirectionClassNumber` |
| The direction a number resolves to comes from the *paragraph*, not the scalar | `base == Unknown` ⟹ the number is `Unknown`, not `LTR` |
| `EN`/`AN` (digits) are a different fact from `ES`/`ET`/`CS` (separators) | both are `Number` *class*, and only a block containing a digit is a number |

The third row is the one that matters at the boundary: `"1,234.5"` is one run because the comma and the
full stop are inside a block that contains digits, and `"Hello, world!"` is one run because they are not.

## 4. The paragraph direction

`RoboTextDirectionContext` is the caller's statement about the paragraph:

| Mode | Meaning |
|---|---|
| `RoboTextDirectionModeExplicitLtr` | CSS's `direction: ltr`, a document's `dir="ltr"`. The text does not get a vote on the *paragraph*, though strong scalars still keep their own direction. |
| `RoboTextDirectionModeExplicitRtl` | The same, right-to-left. |
| `RoboTextDirectionModeAuto` | UAX #9's P2/P3: the first strong scalar decides. |

`fallbackDirection` is what an `Auto` paragraph resolves to when **nothing in the text is strong**, and it
is `Unknown` by default rather than `Ltr`. The reason is honesty about what has been run: P3's level 0 is
the *algorithm's* default, and this stage has not run the algorithm, so `"123 456"` with nothing declared
is `Unknown` — not an LTR claim nobody made. A caller that is laying out UI declares what it wants, which
is why the shell and the document pass `{ Auto, Ltr }` (CSS's initial `direction: ltr`) and why
`"123"` on such a surface is a left-to-right run.

`RoboTextShapeOptions.baseDirection` — the field 12.4 declared — maps onto this with
`RoboTextDirectionContextForBase`: `Unknown` gives `{ Auto, Unknown }`, `Ltr`/`Rtl` give the explicit
mode. `RoboTextDirectionContextExplicit` is the same door for a **span** that carries its own `dir`: the
override lives in the context and never in a mutated Unicode property, and it is analysis input rather
than a change to the text.

## 5. Run invariants

For every input, and for every context:

- a run is non-empty (`start < end`), and never inverted;
- the first run starts at 0 and the last ends at the scalar count;
- `run[i].end == run[i + 1].start` — contiguous, no gaps and no overlaps;
- no two neighbours carry the same direction (coalescing is by construction, not a second pass);
- **empty text produces no runs at all** rather than one empty run;
- the per-scalar array and the run array agree: every scalar inside a run carries that run's direction.

There is no sparse mode, and no API that returns fewer runs than the text needs.

## 6. Cost

One forward pass, one lookahead per neutral block (which stops at the first committed unit), and **no
allocation of its own**: the resolver's only storage is the caller's arrays. The paragraph direction is a
forward scan, so ordinary text never touches the heap; the single path that allocates is an isolate
initiator before any strong scalar, where P2's skip rule needs RoboUnicode's own state and
`RoboTextUnicodeBaseDirection` defers to it.

ASCII never reaches a table: `RoboTextDirectionAsciiClassOf` answers the whole block from comparisons, and
it is **exact** rather than approximate — the weak classes the block contains (`ET` is `#`/`$`/`%`, `ES` is
`+`/`-`, `CS` is `,`/`.`/`/`/`:`) are spelled out, because a shortcut that called a comma neutral would
split `1,234` in an RTL paragraph. `text.direction.ascii_fast_path` proves the shortcut equals the table
over all 128 scalars; `text.direction.performance` records bytes, runs, allocations and time.

## 7. Diagnostics

```text
Direction Analysis
[0..6) scalars  bytes [0..6)  ltr
[6..11) scalars  bytes [6..16)  rtl
```

Both index spaces, always, because a reader outside this layer works in bytes and a reader inside it works
in scalars. `RoboTextDirectionRunWrite` is one run; `RoboTextDirectionRunsWrite` is the table. Both are
complete-or-absent: a buffer that cannot hold the output is left as the empty string and told what it
needed. The table's header, row shape and byte column are shared with the script stage's table
(`RoboTextWriterRunTable`), so the two dumps cannot drift apart.

`Build/Text-GlyphTable.txt` carries a block per phase string with `dir[i]` and `base` rows beside the
script, glyph and measurement rows, which is where the boundaries in this document can be read as evidence.

## 8. Interaction with the other stages

**Scripts.** Direction and script are independent properties read from independent Unicode data:
`if (script == Arabic) direction = RTL` is deliberately *not* the algorithm, because it is also wrong —
a number inside Arabic text is left-to-right, and a punctuation mark's answer depends on both sides
rather than on the script it sits in. The consequence a reader will notice is that a direction run can cut
a script run in two, and that the analysis runs (the intersection) may label a number run with the
surrounding script's name — digits are `Common` in the script stage, which resolves them into a
neighbouring run, while their direction is their own. That is a property of the two stages being
independent, not a defect in either.

**Fonts.** A direction run reaches font selection as one field of an analysis run: the selector is asked
once per **script** run (12.6), and the shape request carries the direction beside the script, the face and
the language. Neither stage reads the other's data to reach its answer.

**Shaping.** `RoboTextShapeRequest.direction` is a real direction for every run the pipeline produces,
which is what a later shaper needs to distinguish Latin LTR from Arabic RTL from Hebrew RTL. Nothing here
shapes, joins, ligates or reorders; those are 12.10 and later, and Arabic joining in particular is
direction-independent.

**Bidi (12.8).** The level runs of the algorithm and the direction runs of this stage **coincide on
ordinary mixed text** — checked against the algorithm's own examples, and against `RoboTextRunSet`'s four
level-run cases in `Tests/Text/SuiteRoboTextRuns.c` — because a run's level is even exactly when this
stage says left-to-right. What 12.8 adds is everything this stage does not have: the level numbers
themselves, the explicit embeddings' contents (this stage classifies the controls as *neutral* and their
contents by the contents' own classes), W2/W4/W5 in full, isolates, and the visual order.

## 9. Limitations

- **No embedding levels and no reordering.** Two pieces of text with the same direction runs can still
  need different visual orders, and this stage cannot express that.
- **The explicit formatting characters are neutral.** `RLE`…`PDF`, `LRI`…`PDI` are classified neutral and
  their content is classified from the content's own classes, so `"abc RLE xyz PDF"` is one left-to-right
  run even though the algorithm would give `xyz` a different level. `RLM` and `LRM` *are* strong and are
  classified as such, because they are the scalars an author uses to steer a paragraph.
- **The paragraph direction is provisional.** It is P2/P3, and 12.8 owns the algorithm it belongs to.
- **`Auto` with no strong scalar is `Unknown`, not P3's default.** A caller that wants P3's level 0 says so
  by declaring `{ Auto, Ltr }` — which is what the shell does.
- **No bidi-aware caret, selection or hit-testing.** Those need levels, not directions.
- **The rendered half is the shell's, and it is unchanged.** `RoboText` links no rasterizer by its own
  boundary, so the phase's direction test page cannot be drawn from this directory. What this stage
  contributes is the table in `Build/Text-GlyphTable.txt` — `dir[i]` and `base` rows per phase string,
  beside the script, glyph and `missing` rows — and the rendered pages
  (`Build/Typography-Desktop.png`, `-Chrome.png`, `-Document.png`) stay as they were: the shipped pack is
  Inter alone, so a Hebrew or Arabic line in that specimen would draw `.notdef` boxes into a picture whose
  only assertion is determinism, which reads as a regression rather than as the pack's coverage limit.
  Making those pages a direction fixture is a **pack** change (assets, manifest, licence, image check), and
  the shell and the document bridge still paint one face per surface.
- **The shell declares its surfaces left-to-right, and the document bridge does not yet.**
  `RoboPaintDecoration.c` passes `{ baseDirection = Ltr }` (the phase's §28 default, declared rather than
  implied, so a digits-only label is left-to-right rather than `Unknown`). The document's text still goes
  through the same single-context call, so a paragraph that declares `dir="rtl"` reaches the document's
  direction only through whatever the author's own rules produce — the document bridge's `direction`
  plumbing is the same increment 12.5 flagged for the analysis runs, and 12.8's levels are what will make it
  draw differently.
- **Nothing draws differently yet, and that is the point of the boundary.** The paint pass reads the
  **layout** command's own direction (`RoboLayoutTextDirectionRtl`, an author/CSS declaration carried by
  `RoboLayoutText.c`, not a second classifier) for its RTL counter, and it reads no RoboText run's direction
  at all: every glyph is drawn left to right from the run's origin, in logical order. So a direction run can
  be asserted end to end — the run set, the analysis runs and every shape request carry it — while no pixel
  moves, which is exactly what "classified, not resolved" should look like at this stage.
