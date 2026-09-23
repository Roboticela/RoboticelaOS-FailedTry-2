# RoboText ligatures (Phase 12.12)

A ligature is the first thing in this pipeline that makes **one glyph** and **one character** disagree:

```text
source      f  f  i          (three characters, three scalars)
glyphs      f f i    →    ﬀi        (the font says so)
```

Phase 12.10 built the machinery that performs it — GSUB, its lookups, its buffers. This phase is the layer
above it, and it answers two questions 12.10 has no business answering:

> **Which ligature features does a caller want?** and **what did the font actually produce?**

Owner: `Runtime/Source/RoboText/RoboTextLigature.c`. Public contract:
`Runtime/Include/RoboText/RoboTextLigature.h`; the value types (`RoboTextLigatureKind`,
`RoboTextLigatureVariant`, `RoboTextLigaturePolicy`) are in `Runtime/Include/RoboText/RoboTextTypes.h`,
beside `RoboTextFeatureSet`, because `RoboTextShapeOptions` carries a policy.

**There is no second ligature engine.** This module writes *feature tags*; GSUB substitutes. A table of
`fi`/`fl`/`ffi` in this tree would be a defect, and the loop that would need one does not exist:

```text
RoboTextLigaturePolicy ──┐
RoboTextLigatureVariant ─┴→ RoboTextFeatureSet → RoboTextGsubPlan → GSUB lookups → glyphs
                                                    (the font's own records)
```

---

## 1. The five kinds, and why they are not a scale

| Kind | Tag | Default | What it is |
|---|---|---|---|
| `RoboTextLigatureStandard` | `liga` | on | the ligatures a reader expects: `fi`, `fl`, `ffi` |
| `RoboTextLigatureContextual` | `clig` | on | the font ligates only in a stated context |
| `RoboTextLigatureRequired` | `rlig` | on | **required** shaping |
| `RoboTextLigatureDiscretionary` | `dlig` | off | decoration: `!?` → `‽` |
| `RoboTextLigatureHistorical` | `hlig` | off | `ct`, `st` |

`RoboTextLigatureKindTag` is the only place a tag literal lives; every other door works in kinds. The five
are independent tags in the feature set, which is what makes `rlig` survivable:

> **An application disabling ordinary ligatures must not accidentally disable required shaping.**

`rlig` is not prettier text. Arabic joins and Indic reordering are *required* substitutions, and a build that
lost them would produce wrong letters rather than a plainer `fi`. So:

- `RoboTextLigatureVariantNone` states `Off` for the four optional kinds and **touches `RoboTextLigatureRequired`
  not at all**;
- `RoboTextLigaturePolicyApply` writes one tag's state and never folds it into another's;
- a policy of `liga=off, rlig=on` is expressible, and is the shape a caller wants when it says "no
  ligatures" about a Latin run.

## 2. The policy, and the rule about being overruled

`RoboTextLigaturePolicy` holds one `RoboTextFeatureState` per kind. `RoboTextFeatureDefault` means *"say
nothing and let the font's own record decide"*, which is not the same as `Off` — that is why the policy is
five three-valued entries rather than five booleans.

```c
RoboTextShapeOptions options;
RoboTextShapeOptionsInit(&options);             /* installs the default policy */
options.ligatures.kinds[RoboTextLigatureDiscretionary] = RoboTextFeatureOn;
```

Two doors, and the difference between them is the whole ordering story:

| Door | Writes | Use it when |
|---|---|---|
| `RoboTextLigaturePolicyApply` | every stated kind, **overwriting** | your layer owns the answer (a stylesheet) |
| `RoboTextLigaturePolicyApplyIfUnset` | stated kinds **not already named** in the set | you are supplying a default under a caller's settings |

`RoboTextRunSetCreate` calls the second one, once, on the run set's feature set:

```text
options.features            the caller's decisions about individual tags
options.ligatures           a default about five tags
        │
        └→ run->features    explicit setting wins; policy fills the rest
```

So `RoboTextShapeOptionsSetFeature(options, "liga", RoboTextFeatureOff, 0)` is not overruled by a policy that
turns standard ligatures on, and both facts are asserted (`text.ligature.feature_set`, `text.ligature.run_set`).

## 3. The stylesheet boundary

`RoboTextLigatureVariant` is the `font-variant-ligatures` vocabulary and
`RoboTextLigaturePolicyFromVariant` is the one translation:

| CSS | policy effect |
|---|---|
| `normal` | states nothing (it is the font's own answer) |
| `none` | the four optional kinds `Off`; `rlig` untouched |
| `common-ligatures` / `no-common-ligatures` | `liga` on / off |
| `contextual` / `no-contextual` | `clig` on / off |
| `discretionary-ligatures` | `dlig` on |
| `historical-ligatures` | `hlig` on |

A variant writes one kind and leaves the others as they were, so two declarations compose —
`none` then `discretionary-ligatures` gives `liga=off, dlig=on`, which is what a stylesheet means by two
declarations in one rule. **No CSS layer writes an OpenType tag**: it names a variant.

There is no CSS parser here and none is planned in this phase. What exists is the vocabulary a parser will
translate into, so that `Documentation/` never has to describe a tag from a stylesheet.

## 4. Required ligatures

Covered in §1, restated because it is the phase's one behavioural rule: `rlig` is not switched off by
`none`, and required substitutions stay under the font's own script/language system. A font that ships no
`rlig` gets nothing, and nothing here invents one.

## 5. Multi-component, longest match, and what the font decides

The number of components is the font's: a `Ligature` record says `componentCount` and lists the glyphs.
This module never sees that count on the way *in*; it reads it on the way *out*, from
`RoboTextGlyph::ligatureComponents`, which the engine records as it substitutes.

Longest-match behaviour is the font's too, and it is ordering rather than a rule in code: a ligature set is
resolved in the order the font states it. The GSUB suite's fixture states `f f f i` *before* `f f i`, and
`fffi` therefore comes out as one glyph — the same bytes in the other order would give `f` + `ffi`. That is
the phase's §"do not arbitrarily select the shortest candidate" as an executable fact
(`text.gsub.ligature`).

## 6. Cluster semantics: what a ligature must not destroy

For `f f i` → one glyph:

```text
glyph.source   = [0, 3)          every scalar it stands for
glyph.ligatureComponents = 3     how many glyphs produced it
scalar → glyph                  both scalars (in general, all of them) answer that one glyph
```

Both facts survive, because a caret will need them:

- `RoboTextGlyphRunGlyphsForScalar` and `…GlyphForScalarIndex` answer *the ligature glyph* for every scalar
  inside it (12.12 §"selection mapping contract"),
- `RoboTextGlyphRunSourceOf` and `RoboTextGlyph::source` answer the whole range for the glyph,
- `glyph.substituted` says a lookup chose the id, and `ligatureComponents >= 2` says the substitution was a
  **many-to-one** one.

Full caret behaviour is 12.14's. What this phase forbids itself is designing the data model so that caret
behaviour becomes impossible later, and the honest statement of today's limitation is that a caret can tell
*which* glyph a scalar belongs to but not yet where *inside* the glyph it should sit.

## 7. Measurement and paint

Nothing here measures or paints, and that is the point:

- **advance** is the resulting glyph's own `hmtx` advance (`RoboTextGlyph::advanceX`), never a sum of the
  components' — `text.ligature.shipped_pack` measures Inter's `!?` ligature and the interrobang on its own
  and requires the same width, so a build that added the two components' advances would fail;
- **`RoboTextMeasurement::width`** is the sum of those advances, rounded once (12.4's rule), so measurement
  follows the shaped result for free;
- **paint** receives glyph runs, so a ligature is drawn because the run says so and not because a painter
  looked at the characters. There is no code-point paint path to bypass the shaped result.

## 8. GPOS: what is *not* here

Ligatures and marks interact — a mark following a ligature attaches to it — and that is positioning, 12.11's
and 12.13's work. In this build a ligature's advance is its own and its offsets are zero, which is exactly
the state `RoboTextCapabilities.positioning = 0` reports. Nothing in this file assumes anything about what
positioning will do with a ligature glyph, and the glyph record keeps what it will need.

## 9. Diagnostics

```text
RoboTextLigaturePolicyWrite     "liga=on clig=on rlig=on dlig=off hlig=off"
RoboTextLigatureReportWrite     "ligatures=1 absorbed=2 max=2 glyphs=1 scalars=2 substituted=1"
RoboTextGlyphWrite              "… from=2 …"     (the component count)
RoboTextLigatureKindName/Tag    the vocabulary a message is built from
```

A dump that does not fit its buffer is **refused** rather than truncated: a shorter line is a different
record, which is the one thing a dump must not silently become.

**Why a ligature did not form** is answered by the pieces rather than by a bespoke explanation API, and the
mapping is worth writing down:

| Symptom | Where it shows |
|---|---|
| feature off by policy | `RoboTextLigaturePolicyWrite`, or the run's feature set |
| caller's explicit setting | the run's `features` (`RoboTextFeatureSetFind`) |
| the font has no such feature for this script/language | `RoboTextGsubPlan` (`text.gsub.plan`) |
| the feature is on but its coverage missed | `RoboTextGsubStats` (`subtablesTried`, `ligatureSubstitutions`) |
| a lookup was skipped as malformed | `RoboTextGsubStats::lookupsSkipped` + the diag record |
| a context did not match | `RoboTextGsubStats::contextualMatches` |

If a future caller wants one call that answers all six, it belongs here — as a *summary* over those
records, not as a second implementation of them.

## 10. Test fonts, and what the shipped pack can and cannot show

`Tests/Fonts/` holds no dedicated ligature font, deliberately: 12.10's synthetic GSUB fixture already
carries a ligature (and, since 12.12, two of them in one set), and building a second GSUB font to hold the
same bytes would be two fixtures disagreeing about `ffi`. So the evidence is split by what each face can
honestly prove:

| Evidence | Font | Where |
|---|---|---|
| standard ligatures, longest match, 2/3/4 components, provenance | the GSUB fixture (built in-code, byte by byte) | `Tests/Text/SuiteRoboTextGsub.c`, `text.gsub.ligature` |
| policy, variants, precedence, report, glyph flags | the shipped pack (no substitution needed) | `Tests/Text/SuiteRoboTextLigature.c`, `text.ligature.policy` |
| a **real** ligature end-to-end | the shipped pack | `text.ligature.shipped_pack` |

The shipped Inter carries `dlig` but **no `liga`**, and that is worth stating because a reader who expects
`office` to ligate with the system UI font will not see it ligate. What Inter does carry is real and
decisive: `!?` → `‽` (U+203D) and `!!`/`??` → `⁉` (U+2E18) under `dlig`, through a chained-context lookup
and a ligature lookup. So `text.ligature.shipped_pack` shapes `!?` twice — once with the default policy and
once with `discretionary-ligatures` — and requires two glyphs then one, with the one covering both scalars
and measuring to the interrobang's own advance.

No host-only rendering path is involved: the run is the ordinary `RoboTextShape` result, and the QEMU
gallery draws through the same pipeline.

## 11. Performance and thread safety

- The policy is a 20-byte value copied with the options; applying it walks five entries once per run set.
- Nothing here parses a table, allocates per glyph, or caches anything: the GSUB table is parsed once per
  face (12.10), and every parse involved is immutable and shared.
- A report walks the glyph array once, reading fields the generator already filled.
- No mutable global state exists in this module, so two threads shaping two documents share nothing.

## 12. Known limitations

1. **No `liga` in the shipped pack.** Standard-ligature behaviour is proven against the synthetic fixture,
   not against the system UI font. A pack change that added a `liga` face would be the way to change that.
2. **No GPOS.** A mark after a ligature is not positioned by this build at all; see §8.
3. **No range-scoped policy.** A policy applies to a whole run set. Per-range overrides need the range
   model 12.9's feature work would bring, and the run's feature set is already per-run so the seam is there.
4. **No caret geometry inside a ligature.** §6: the mapping exists, its use does not yet.
5. **`clig` and `hlig` are exercised as tags, not as substitutions.** No face in this tree ships a
   contextual-only or historical ligature, so the suite proves the tag reaches the plan and the font decides
   the rest; the standard and discretionary paths are proven end-to-end.
6. **The paint and layout paths shape with the default policy.** A display list's text command carries the
   characters and the setting it was *measured* with, and nothing yet carries a ligature policy, so the
   desktop draws text with `liga`/`clig`/`rlig` on and `dlig`/`hlig` off — the default. Setting a policy per
   document is the CSS property's job (`font-variant-ligatures`, a later phase) and needs one field on the
   text command; until then a caller that wants a non-default policy shapes through `RoboTextShape` itself.
   This is why the phase's visual-regression item is discharged by the real-font assertions in
   `text.ligature.shipped_pack` rather than by a screenshot of two ligature settings.
