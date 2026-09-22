# RoboText ↔ Font integration (Phase 12.5)

How the text layer turns "a code point plus a requested setting" into a real installed face, and what it
does with that face's numbers. The governing sentence:

> **Unicode determines what the text is; fonts determine how that text can be represented.**

| Fact | Owner |
|---|---|
| Discovery, loading, the registry, the pack, `cmap`, `hmtx`, `head`/`hhea`/`OS/2` | **RoboFont** (Phase 5, 12.2) |
| Role → face (UI Text, UI Display, Monospace) | **RoboTypography** (12.3) — above RoboText |
| Coverage-gated selection policy, font identity, fallback order | **RoboText** (`RoboTextFontSelection.c`) |
| Glyph id, advance, metrics scaling | **RoboText** wraps RoboFont's doors (`RoboTextFont.c`) |
| Substitution / positioning / kerning | later goals; all reported absent |

RoboText never scans a directory, opens a file or names `/System/Fonts/`. It is handed a primary context and
an ordered fallback chain, and it chooses among them.

---

## 1. Coverage

`RoboTextFontFaceSupportsCodePoint(face, cp)` is the coverage door. It calls RoboFont's lazy, idempotent
`FontFaceParseCoverage` (the `cmap` is parsed **once** per face) and reads the immutable range table via
`FontCoverageContains`. Coverage means a **usable** mapping: a scalar mapped to glyph 0 (`.notdef`) does not
count, and neither does a non-scalar. Agreement with the cmap is pinned by a test that compares the answer to
`FontFaceLookupGlyph` for a sample of scalars.

`RoboTextFontFaceSupportsRange` is the run-level form (how many of a sequence are missing) — the form a
fallback decision on a **coherent sequence** needs.

The selector adds a 256-slot direct-mapped **memo** keyed by `(face, code point)`; its counters are exposed
via `RoboTextFontSelectorCacheStats` so a test proves the repeat path is exercised. It is a repeat-catcher,
not a coverage database — RoboFont already owns that.

## 2. Selection order (deterministic, data-driven)

```
primary context  →  requested weight / style / stretch (already baked into the context)
                 →  coverage check for the code point
                 →  fallback chain, in the chain's own order
                 →  none (the honest answer, not a failure)
```

`RoboTextFontSelectorSelectScalar` picks the first candidate with usable coverage. There is no scoring and no
randomness: the chain's **order is the policy**, which is what makes a fallback change a data change and
"same configuration ⇒ same selection" a testable fact. `RoboTextResolvedFontCompare` gives a total order.

`RoboTextFontSelectorSelectRange` prefers a candidate covering **every** scalar (`complete = 1`); failing
that, the first covering the run's base (`complete = 0`); failing that, `none`. The run-level door exists so a
base character and its combining mark are one decision, not two.

## 3. Font identity

`RoboTextResolvedFont` is the identity a run carries — never a font file:

- `faceId` (Phase 5's stable face identity) and the borrowed `FontFace *` for the immediate caller;
- the requested rendering parameters (`sizePx`, `weight`, `style`, `stretch`);
- the provenance: `RoboTextFontSourcePrimary` / `…Fallback` / `…None` and `candidateIndex`;
- `complete`, for a range selection.

Equality and ordering are over the identity fields, **not** the pointer, so a run split is stable across
copies and two selectors produce comparable records.

## 4. Metrics and scaling

`RoboTextFontContextMetrics` reports `ascentPx`, `descentPx`, `lineGapPx`, `lineHeightPx` and — since 12.5 —
`capHeightPx` / `xHeightPx` **when the face declares them** (`OS/2` v2+; 0 means "not available", never a
guess). `RoboTextFontUnitsToDevice` is the module's one scaling door: `(value * sizePx) / unitsPerEm`,
`double` in, `float` out, using the *face's actual* `unitsPerEm`. No metric is estimated from the font size
alone, and no face is assumed to have a particular em square.

## 5. Analysis runs

`RoboTextAnalysisCreate` produces `RoboTextAnalysisRun`s: maximal spans over which **script, classified
direction and selected font all agree**. A run carries no glyphs — substitution (12.10) and positioning
(12.11) fill that in later. The ranges stay in scalars, so a caret, a selection and a hit test address the
same text.

**Combining marks do not start a font.** A mark takes the font of the scalar before it, whatever the selector
would have answered for the mark on its own. `"e" + U+E0100` — a mark no face covers — is one run, not a
covered base plus an unresolved mark.

`RoboTextShapeRequest` is the fully populated request: text view, range, `RoboTextFontContext` (face +
setting), script, direction, declared base direction, language, features. `RoboTextShapeRequestToRun` turns
it into the `RoboTextRun` the 12.4 generator consumes, so the analysis path reaches real glyphs through the
existing pipeline.

## 6. Language

`RoboTextLanguage` is carried on the request and every run and read by nothing yet. Language never determines
script: a `"ur"`-tagged string of Latin letters is Latin. Shaping's `locl` and a language-preferred fallback
will read it in later phases.

## 7. Boundaries and thread safety

RoboText may include `RoboBase`, `RoboMem`, `RoboUnicode`, `RoboFont` and nothing above. A selector is
mutable per thread; the **inputs** (face coverage, font metrics, Unicode tables) are immutable and safely
readable concurrently. There is no mutable global "current font".

## 8. Tests

`Tests/Text/SuiteRoboTextFontSelection.c` (`text.fontselection.*`) and
`Tests/Text/SuiteRoboTextAnalysis.c` (`text.analysis.*`). The pack ships one family (Inter), so the suites
exercise the fall-through path with a **no-face primary** plus a one-entry chain; when a CJK or emoji family
joins the pack, adding it to a chain is the only change those cases need.

A font-integration change is not done until `TestRoboText.ps1 -Target Host` **and** the suites for the
boundaries it touched pass — `TestLayout.ps1`, `TestPaint.ps1`, `TestGraphics.ps1`, `TestFont.ps1` and
`TestDesktop.ps1` (all `-Target Host`).
