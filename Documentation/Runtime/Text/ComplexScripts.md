# Complex-script shaping (Phase 12.19)

RoboText shapes scripts whose rendering depends on context: a letter whose shape is decided by its neighbours
(Arabic), and a syllable whose elements are written in a different order from the one they are drawn in
(Devanagari). This document is the phase's authority, beside
[`GlyphSubstitution.md`](GlyphSubstitution.md), [`Positioning.md`](Positioning.md) and
[`GlyphRuns.md`](GlyphRuns.md).

The one rule, and everything below is a consequence of it:

> **A complex script's behaviour is a *plan*, not a branch.** A script's shaping is data — a strategy, stages of
> features that say *where* they apply, and reorder steps between them — so adding a script is adding a row, and
> no renderer, display list, layout engine or font is ever told which script it is shaping.

---

## 1. Where the code lives

| File | Owns |
|---|---|
| `Runtime/Source/RoboText/RoboTextComplex.c` | the script → strategy table, the per-run plan resolver, the cluster vocabulary, the reorder dispatcher, `RoboTextComplexIsSafeBoundary` |
| `Runtime/Source/RoboText/RoboTextArabic.c` | joining resolution, the four contextual forms and their masks, the joining clusters, the Arabic feature stages |
| `Runtime/Source/RoboText/RoboTextIndic.c` | the per-script rules, syllables and their base, the position masks, the initial and final reorders, the Indic stages |
| `Runtime/Source/RoboUnicode/RoboUnicodeJoining.c` | the `Joining_Type` property (UAX #44), which is the Arabic shaper's only Unicode input |
| `Runtime/Source/RoboText/RoboTextGsub.c` / `RoboTextGpos.c` | the engines. They gained a positional test and (GPOS) lookup types 3–6; they gained no script. |

Public contracts: `RoboTextComplex.h`, `RoboTextArabic.h`, `RoboTextIndic.h`, and
`RoboUnicodeJoining.h` for the property.

**Division of labour (12.19 §26).** The plan says which features run, in which stage, at which positions, and
whether the buffer is reordered between stages. GSUB substitutes. GPOS positions. Bidi resolves direction and
order. Clusters say what shapes as one unit. No one of them does another's job, and the module parses no font
and keeps no Unicode table.

---

## 2. The strategy, and the plan over it

`RoboTextComplexStrategy` names **families of behaviour**, not scripts: `None`, `Arabic`, `Indic`,
`SoutheastAsian`. Script identity stays in Unicode analysis (12.6's script runs); the mapping is one table
(`RoboTextComplexPlanForScript`), which is why "add Kannada" is a data change.

Each row is a `RoboTextComplexScriptPlan`: the strategy, the OpenType script tag (`arab`, `deva`, …) and the
script's own facts — has a reph, writes half forms, has pre/base/post/below-base elements, has a nukta, has a
virama. A script with no row answers the `None` row, so a caller never asks twice.

`RoboTextComplexPlanResolve(script, features, &plan, diag)` builds the **per-run** plan:

1. the strategy's own module emits its stages (`RoboTextArabicStages`, `RoboTextIndicStages`);
2. the caller's `RoboTextShapeOptions::features` is applied **as an override** — a tag turned `Off` is
   *removed* from its stage, a tag the caller did not name keeps the strategy's default.

That precedence is 12.12's ligature rule one layer down, and it is what makes
`font-feature-settings: "init" 0` mean something (`text.arabic.feature_overridden`).

A stage is `RoboTextComplexStage`: a name, up to `RoboTextComplexMaxStageFeatures` features each with a mask,
and a `reorderBefore` / `reorderAfter` step. The stages are the **complete** answer for their features — the
glyph generator resolves each stage with *no* script defaults — because handing the script's defaults in would
let a feature the stage does not own run in every stage, which is how an Indic conjunct comes to be half-formed
twice.

---

## 3. Positional masks — the one new mechanism

Two facts, and they meet in `RoboTextOtMaskAllows`:

* the **position's** mask word, one `uint32_t` per scalar, from the strategy
  (`RoboTextArabicMasksBuild`, `RoboTextIndicMasksBuild`);
* the **feature's** mask bit, one `uint16_t` per lookup, from the stage
  (`RoboTextComplexStageApplyMasks`).

A lookup applies at a position when the lookup's mask is 0 (`RoboTextComplexMaskAll`, which is every Latin,
Greek and Cyrillic run) or the position's word ANDs with it. Both engines ask the question at the position a
match **starts**, which is why a lookup that begins at a sequence's leftmost glyph is masked by that glyph's
scalar rather than by the base's.

Two rules that are easy to get wrong, and are therefore stated:

* **`RoboTextOtPlan::lookupMasks` is indexed by the Lookup List index**, which is the index the engine reads it
  with — *not* by a position in `lookupOrder`. A plan that enables lookups 0, 4, 5, 6 and 7 would otherwise
  write a mask for lookup 4 into slot 1 and the engine would read a zero for lookup 4.
* **A reorder does not move a mask.** The masks are computed over the *original* scalar indices and looked up
  by the source scalar a glyph came from, so a glyph carries the position class of the character it was made
  from — which is what makes "'half' applies to the first consonant and not to the base" survive a move.

---

## 4. Clusters

`RoboTextComplexCluster` is the smallest stretch of source that must shape — and break — as a unit: a source
range in the text's **scalar** index space, in logical order, plus the kind, the base's index among its own
scalars, and the flags a reorder and a line breaker need (`hasReph`, `hasPreBase`, `hasPostBase`,
`hasBelowBase`, `hasVirama`, `hasNukta`, `unsafeToBreak`).

* **Arabic** groups a joining sequence: two scalars belong together when the second is transparent or when the
  first can give a connection and the second accept one — the same question the form resolver asks, asked for
  boundaries rather than for shapes. `ZWNJ` is non-joining, so it ends one sequence and begins another, which
  is exactly what the character is for.
* **Indic** groups a syllable: a consonant, its viramas, its matras and its marks, with the **base** being the
  last consonant that is not followed by a virama. A consonant after a consonant starts a new syllable — the
  UCD's "an inherent vowel ends the consonant sequence", stated here because `Indic_Syllabic_Category` is not in
  this tree.

`RoboTextComplexIsSafeBoundary(clusters, count, at)` answers §28: a cluster's own start is a boundary **unless**
the cluster before it claimed the two must stay together. A trailing virama attaches forward (the half form it
produces belongs to what follows), a joining sequence may not be cut, a stray mark cluster with no base is unsafe
forward. This is groundwork for the line breaker, the caret and incremental shaping — it answers a question and
implements nothing.

---

## 5. Arabic

### 5.1 Joining

`RoboTextArabicJoiningResolve(view, range, out, capacity, &count, diag)` answers one `RoboTextArabicForm` per
scalar: `None`, `Isolated`, `Initial`, `Medial`, `Final`. It reads each scalar's `Joining_Type` from RoboUnicode
(§9 of the phase: no Unicode range is hard-coded here) and walks to the nearest **participating** neighbour,
stepping over transparent scalars.

Two rules with numbers on them in `Tests/Text/SuiteRoboTextArabic.c`:

* **A transparent scalar is invisible to joining and takes no form of its own.** `ب` + fatha + `ب` is
  initial, `None`, final — the connection survives the mark, and the mark is not asked for `medi`.
* **A right-joining letter is not "a final letter" by accident.** `ALEF`, `DAL`, `REH`, `WAW` accept a
  connection from the letter before them and never give one onwards, which is why `لا` is initial lam + final
  alef.

`RoboTextArabicFormMask` maps a form to its bit and `RoboTextArabicMasksBuild` turns the form array into the
position words the engine reads.

### 5.2 Features

`RoboTextArabicStages` emits three stages:

| Stage | Features | Masks |
|---|---|---|
| `arabic.common` | `ccmp`, `locl` | none |
| `arabic.forms` | `isol`, `init`, `medi`, `fina`, `fin2`, `fin3`, `med2` | one bit per contextual form |
| `arabic.ligatures` | `rlig`, `calt`, `liga`, `clig`, `dlig` | none |

The contextual forms are **one stage**, not four passes: the four features are alternatives *at a position*, and
the positional test is what keeps `init` off a medial letter. `fin2`/`fin3`/`med2` are the Syriac-valued
alternates and are harmless for a font that lists none.

The **lookup order is the font's**, not the stage's: `RoboTextOtPlanResolve` collects each enabled feature's
lookups and sorts them by Lookup List index. So a face that numbers `rlig` before `isol` ligates the *nominal*
letters, and a face that numbers it after ligates the formed pair — which is why the phase's fixture states its
`rlig` rule over `init` lam + `fina` alef, the input a real Arabic face has.

The lam + alef ligature is **GSUB, not a special case**: the substituted glyph keeps the union of its
components' source range and `ligatureComponents >= 2`, so selection and copy still reach both scalars.
(`text.arabic.lam_alef_ligature` reads the id, the source range, the cluster and the advance.)

### 5.3 Marks and cursive connections

Neither is Arabic code. They are GPOS types 4, 6 and 3, which this phase gave the engine:

* **mark-to-base** puts the mark where the base's anchor is: `offset = (basePlacement + baseAnchor) −
  (penDistance + markAnchor)`;
* **mark-to-mark** stacks a second diacritic on the first, by the same formula against the preceding *mark*;
* **cursive attachment** is the format's two statements — the pen after the first glyph lands on the anchor it
  leaves by, and the second glyph's receiving anchor is placed where that pen is. The first is an advance and
  the second a placement, which is why an attachment needs the face's own advance: a distance GPOS does not
  carry. Which anchor is the outgoing one is the direction convention, and for Arabic the run is
  right-to-left, so the first letter leaves by its *entry* anchor.

A face with no advance to offer — no metrics, or a caller that supplied no font context — is counted
`lookupsUnsupported` **once per lookup**, not once per position.

---

## 6. Indic

Devanagari is the implemented script; the same architecture carries the rest through a rule row.

### 6.1 Rules

`RoboTextIndicScriptRules` is one row per script: its virama, its nukta, and the four positional classes of its
vowel signs. A script with **no** row is refused rather than shaped with another script's virama — shaping
Kannada with Devanagari's rules would be worse than shaping it plain. Bengali, Gujarati, Gurmukhi, Oriya,
Tamil, Telugu, Kannada, Malayalam and Sinhala carry their own virama and nukta (so `RoboTextIndicIsVirama`
answers correctly for them) and **no** positional classes, so nothing is reordered for them: a named
limitation rather than a silent reorder by Devanagari's vowels.

`RoboTextIndicIsConsonant` is `Lo`, not the virama, not the nukta, not a positional class, and of the script.

### 6.2 Syllables and the base

One forward scan: a consonant, then a nukta, then either a virama (which continues the syllable with the next
consonant, stepping over a `ZWJ`/`ZWNJ`) or a matra or a mark (which attaches). Anything else ends the
syllable. The **base** is the last consonant not followed by a virama; when no consonant survives as a base, the
base is the last consonant seen, or the cluster's first scalar.

The reph is `ra` + virama at the *start* of the syllable's shaping sequence, followed by a consonant —
`IndicFindRephScalar` is the one implementation, called by both the classifier and the two reorder steps,
because a reph the classifier found and a reorder could not locate would be two readings of one syllable.

### 6.3 Masks and stages

`RoboTextIndicMasksBuild` turns the syllables into one word per scalar:

| Bit | On |
|---|---|
| `Reph` | the `ra` of the syllable's reph — the scalar the `rphf` lookup is tried at |
| `Half` | a consonant a virama follows (so the trailing `क` `्` halves rather than basing) |
| `Below` | a consonant at or after the base |
| `Post` | a consonant after the base |
| `Pre` | the first consonant of a multi-consonant syllable that is not the base |
| `Conjunct` | every consonant of a multi-consonant syllable — a conjunct lookup begins at the sequence's leftmost consonant, so marking only those after the first would leave `half` and `cjct` unable to match the half form `half` just made |
| `Consonant` | any consonant in the syllable |

Three stages: `indic.common` (`ccmp`, `locl`), `indic.substitute` (`nukt`, `akhn`, `rphf`, `rkrf`, `vatu`,
`pref`, `blwf`, `abvf`, `half`, `pstf`, `cjct` — the masked ones), and `indic.presentation` (`pres`, `abvs`,
`blws`, `psts`, `haln`, `calt`).

### 6.4 Reordering

Source order is not shaping order and shaping order is not visual order, and all three are kept distinct.

* **Initial** (`RoboTextComplexReorderInitial`, `reorderBefore` on the first stage): a pre-base vowel sign
  moves to immediately **before the base**, so the consonant features see consonants adjacent to their viramas
  rather than a vowel sign in the middle of a conjunct. The reph needs no move here, which is a property of
  `IndicFindRephScalar` (it only finds a reph at the syllable's start) rather than an omission.
* **Final** (`…Final`, `reorderAfter` on the substitution stage): the reph glyph moves to immediately *after*
  the glyph covering the base, because Devanagari draws it above the base it followed. After the substitution
  stage, because until `rphf` has run there is no reph glyph to place.

Both moves are on the **shaping buffer** and by *glyph*: every glyph keeps its `source`, so a caret, a selection
and a copy still read the characters in the order the author wrote them. `text.indic.reph` reads the ids
(`[म, र्]`) *and* the source ranges (`2..3` and `0..2`) to keep the two orders distinguishable.

---

## 7. Diagnostics

| Door | Writes |
|---|---|
| `RoboTextComplexPlanWrite` | one line per stage with its features and masks |
| `RoboTextComplexClustersWrite`, `RoboTextIndicClustersWrite` | one line per cluster: range, kind, base, the flags that are set |
| `RoboTextArabicJoiningWrite` | one line per scalar: index, form and mask (`"#0 form=initial mask=2"`) |
| `RoboTextComplexStrategyName`, `RoboTextComplexClusterKindName`, `RoboTextArabicFormName` | the vocabularies |

Every writer is **complete-or-absent**: a buffer that cannot hold the record is refused
(`RoboNotSupported`) rather than truncated, because a truncated record reads as a different record.
`text.arabic.diagnostics`, `text.indic.diagnostics` and `text.arabic.plan` exercise both halves.

---

## 8. What this build does not claim

Named rather than implied, because a plan that silently degraded would render wrong text with correct counts:

* the **Southeast Asian** strategy (Thai, Khmer, Myanmar) has stages of its own to gain and has none today;
* `blwf`'s interaction with `rkrf`/`vatu` for below-base forms that need their own reordering;
* the `haln`/`cjct` "base becomes a half form with the last consonant" refinement;
* the UCD's `Indic_Positional_Category` as the data source — the script rules are this module's tables;
* the **other Indic scripts'** own reorder rules: their rows carry `hasReph` and the Devanagari rule set is
  applied, which is right for Devanagari and unproved for the rest;
* **Arabic's joining groups** — the Syriac, N'Ko, Mandaic, Mongolian and Sogdian special cases — which share the
  strategy and would need their own joining tables;
* **canonically reordered input**: no normalization is performed and no second normalization engine exists here
  (§29); the source code points are preserved exactly and the corpus states what that means;
* **font fallback at a complex boundary** (§25 is a *policy* here, not a mechanism): the clusters name what must
  not be split, and the fallback unit a future selector uses is a cluster rather than a scalar.

`RoboTextGetCapabilities` is the honest record: `complexScripts`, `complexScriptStrategies`, `complexClusters`,
`positionalFeatures`, `gposAttachments`, `reordering` and `complexDiagnostics` are `1`, and
`verticalText` is `0` because Phase 12.20 owns it.

---

## 9. Tests

`Tests/Text/SuiteRoboTextArabic.c` (`text.arabic.*`) and `SuiteRoboTextIndic.c` (`text.indic.*`) over the
**complex fixture** `Tests/Text/RoboTextComplexFont.h`: one built face carrying both scripts, because a face's
ScriptList decides which features a run reaches and a mixed-script case needs one face with two records. The
fixture's bytes state every expected number — each (letter, form) glyph id, the lam/alef ligature, the reph, the
half form, the conjunct, fatha's and shadda's anchors, beh's cursive entry and exit, and the zero advance of a
mark — so a case reads as arithmetic over the font rather than as a number someone once observed (§31).

Two rules the fixture follows, because both are ways a complex fixture lies:

* the **GPOS table exists whenever the spec has any rule kind at all**. A face whose only positioning is an
  attachment (Arabic's, Devanagari's) must not ship no GPOS table — that is how a mark case comes to read
  zeroes and look like an engine defect.
* a rule's **input is the glyph the earlier stage produced**. `rlig` runs after the contextual forms, so its
  components are `init` lam and `fina` alef; a rule over the nominal ids would be a rule no font could use.

What the fixture does not state, and is therefore asserted as a *mask* rather than as shaped output: no
`blwf`/`pstf`/`pref` rule (their bits are named in `text.indic.masks`), no `locl`, no `calt`, no `nukt`/`akhn`.

**Two acceptance criteria have their own cases rather than being assumed.** §13 — a malformed or unsupported
sequence answers for itself and corrupts nothing — is `text.arabic.stray_mark` (a mark with no base: no
attachment, no advance, a valid run, its own cluster), `text.indic.orphan_sign` (a virama with no consonant
before it, and a matra before any consonant) and `text.arabic.no_layout_tables` (a face with neither GSUB nor
GPOS shaped with the Arabic strategy: the `cmap`'s answer, the right classification, the right width). §10 —
mixed-script documents — is `text.indic.three_scripts`: Latin, Arabic and Devanagari in one logical string, three
runs in logical order, each shaped by its own strategy's stages and by nothing else.

Run:

```powershell
Scripts\Test\TestRoboText.ps1 -Target Host                    # text.*, 277 cases
Scripts\Test\TestLayout.ps1   -Target Host
Scripts\Test\TestPaint.ps1    -Target Host
Scripts\Test\TestGraphics.ps1 -Target Host
Scripts\Test\TestFont.ps1     -Target Host
Scripts\Test\TestDesktop.ps1  -Target Host
```

A complex-script change is not done until all six pass, and `-Target Qemu` is required when the guest image is
involved.

**Two deviations from the phase's §36 and §43, stated rather than papered over.** There is no separate
`BuildRoboTextComplexScripts.ps1` / `TestRoboTextComplexScripts.ps1`: the subsystem's own entry points
(`BuildRoboText.ps1`, `TestRoboText.ps1`) compile these modules into the one RoboText library and register the
suites in the one `text.*` runner, which is this repository's rule of one build and one test script per
subsystem — a second pair would split a single library's suites across two runners and two case counts. And
there is **no QEMU visual page** for Arabic or Devanagari: the complex fixture is a face this repository builds
at *test* time, and the shipped pack carries neither script, so a guest page today could only render
`.notdef`. A guest specimen becomes meaningful in the same change that adds a complex-script face to the system
typeface pack (assets + manifest + licence + the image build's check) — a **pack** change, not a text one,
and one that should be made deliberately rather than to satisfy a checkbox.
