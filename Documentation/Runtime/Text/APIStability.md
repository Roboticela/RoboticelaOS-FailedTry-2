# RoboText — API stability

Phase 12.24 froze RoboText as a subsystem: one public API, one shaping pipeline, one font-selection path, one
measurement path, one glyph-run contract, one painting bridge. This document is the contract a consumer builds
against, and the record of what is deliberately *not* frozen. `Overview.md` is the map; this is the fine print.

> **New runtime components must use the frozen RoboText API rather than introducing parallel text systems.**
> The four rules that are mechanically checkable are enforced by `Assert-RoboTextIntegrationRules` in
> `Runtime/Tools/Build.ps1` (`Overview.md` §4).

---

## 1. Stable, internal, experimental, deprecated

### Stable — frozen in 12.24

The seam tier of `Runtime/Include/RoboText/RoboText.h`: `RoboTextBuffer`/`RoboTextRange`/`RoboTextView` and
the four index spaces; `RoboTextFontStyle`/`RoboTextFontContext`; `RoboTextShapeOptions` and
`RoboTextShape`/`RoboTextShapeResult*`; `RoboTextRunSet`; `RoboTextGlyphRun`/`RoboTextGlyphRunList`;
`RoboTextMeasurement` and the measure doors; `RoboTextSelection`/`RoboTextCaretGeometry`/the selection map;
`RoboTextCaret`/`RoboTextEditOperation`/`RoboTextEditResult`/`RoboTextEditInvalidation`/`RoboTextComposition`
and the editor; `RoboTextCapabilities`/`RoboTextVersionString`.

Stable means: a field may be **added** (the structs are extended at their end), a name is not removed or
repurposed, and a documented answer is not replaced by another documented answer, without a change to the
version pair and a row in this document. `Tests/Text/SuiteRoboTextApi.c` (`text.api.*`) is the suite that
protects the shape of that surface: the aggregate's completeness, the initial values, the refusals and the
borrowing rules.

### Advanced-public — frozen as records, not as an interface

`RoboTextGsub.h`, `RoboTextGpos.h`, `RoboTextOtLayout.h`, `RoboTextComplex.h`, `RoboTextArabic.h`,
`RoboTextIndic.h`, `RoboTextLigature.h`, `RoboTextKerning.h`, `RoboTextGlyphs.h`. Their *records* (plans,
events, stats, policies) are stable and read by the conformance suites; the interpretation of lookup lists,
coverage tables and class definitions behind them is internal. A consumer of text must not include them
(`Overview.md` §2) — the glyph run is what it wants.

### Internal — not part of the freeze

`Runtime/Source/RoboText/RoboTextInternal.{c,h}` and every module-private static: the lookup reader, the
shaping buffer, the temporary analysis buffers, the cluster resolver's scratch, the cache internals, and the
per-lookup implementations in `RoboTextGsubSubst.c` and `RoboTextGpos.c`. Anything under
`Runtime/Source/RoboText/` that is not in a public header may change in any commit.

### Experimental — deliberately not frozen

Variable-axis application (`RoboTextCapabilities.variableAxisApplication` is 0), letter spacing and
optical-size instancing (carried on `RoboTextFontStyle` and **not applied**), a cluster-grained font selector
(12.19 names it), Southeast Asian script stages, `Vertical_Orientation` from RoboUnicode, vertical block
progression and vertical line breaking, a guest page in a vertical writing mode, a document path with a
writing mode or a selection at all, and `word`/`line` editing boundaries (`wordBoundaries` and
`lineBoundaries` are 0, and `textEditing.undoHistory` is 0 — the *record* is in every operation, the manager is
not). Each absence is a value in `RoboTextCapabilities` rather than a silence in prose.

### Deprecated

One interface was deprecated **by this phase** and removed rather than kept as a wrapper:
`RoboDesktop`'s `DesktopChromeUtf8Next` (a private UTF-8 decoder) and its per-code-point glyph resolution,
which `RoboDesktopChrome.c` used to draw its labels. There is no compatibility shim: the shell's labels are
shaped and drawn through `RoboTextShape`/`PaintRunDraw`. Nothing else in the tree paints text from code points,
so no other legacy path exists to quarantine.

---

## 2. Ownership

One model, everywhere:

| Object | Owns | Borrows |
|---|---|---|
| `RoboTextBuffer` | its UTF-8 bytes | — |
| `RoboTextView` | nothing | the caller's bytes, for the duration of one call |
| `RoboTextFontContext` | a *copy* of the style | the `FontFace`, which must outlive every call made with the context |
| `RoboTextShapeResult` | its run set, glyph runs, measurement | the face passed to the call it was built by (it records ids and metrics, not the face) |
| `RoboTextRunSet`, `RoboTextGlyphRun`, `RoboTextGlyphRunList`, `RoboTextMeasurement` | nothing | the `RoboTextShapeResult` they came from — destroyed with it |
| `RoboTextSelectionMap` | its copied placements | the run list it was built over |
| `RoboTextEditor` | one buffer, one selection, one caret, one operation record | the cluster resolver's run list, if one is installed |
| `PaintGlyph` arrays | the caller (`RoboFree`) | — |
| `RoboFont`'s faces | the font registry | — |

**No raw pointer implies ownership.** A door that returns a pointer either returns a *borrowed* one, named as
such in its header, or hands back an allocation the caller releases and says so (`PaintRunShape.h` is the
worked example). Accessors are safe on NULL and answer NULL or zero rather than trapping
(`text.api.refusals`).

**Font lifetime.** A `FontFace` must outlive every call made with it, and `RoboText` never retains one. Paint's
display lists keep a pointer-free **token** (`PaintFontToken`) resolved through the list's own font table, so a
record is safe to copy, to rebuild and to hand across a frame boundary; the table's lifetime is what makes a
glyph id meaningful.

**Result immutability.** Every result type is immutable after construction: `RoboTextShapeResult`, its run list,
its glyph runs, `RoboTextMeasurement`, `RoboTextSelectionMap`. No door takes a non-const result, and a renderer
cannot mutate a glyph run because no door exists that would let it. Caching, threading and debugging all rest on
this.

---

## 3. Lifetime rules a consumer can rely on

- A view is valid for the call it is passed to and no longer.
- A result's parts are borrowed from it and are valid until `RoboTextShapeResultDestroy`. Two reads of the same
  part answer the same pointer (`text.api.borrowing`).
- `RoboTextShapeResultDestroy(NULL)` and `RoboTextBufferDestroy(NULL)` are legal no-ops, which is what makes an
  error path's cleanup readable.
- Shaping twice from the same inputs produces two independent results; nothing is retained between calls and no
  global state carries input to input (`text.api.stress` measures one text twenty-five times).
- `RoboTextFontContextSet` copies the style and borrows the face; an initialised context with no face is a
  **supported** configuration — every door that needs a face answers `RoboNotSupported` instead of crashing.

---

## 4. Threading

| Kind | What may be shared |
|---|---|
| **shared, read-only** | Unicode property tables, `FontFace` tables once parsed, the system pack, a `RoboTextShapeResult` and everything borrowed from it, a `RoboTextMeasurement`, a `RoboTextSelectionMap` |
| **per operation** | the shaping buffer, the GSUB/GPOS passes and their plans, the temporary analysis arrays, the cluster resolver's scratch, a `RoboTextEditor` and its operation record |
| **mutable global** | none |

A `RoboFont` face's *first* parse is the one thing that is not free of side effects (`ParseCmap`,
`ParseMetrics`); a process that shares a face across threads parses it once before the threads start —
`RoboDesktop.c` does exactly that at the door a face enters the shell, and `RoboPaintDecoration.c` does it
before a paint pass. Caches a consumer adds are the consumer's to make thread-safe; RoboText ships none of its
own beyond the coverage cache in RoboFont and the pack metadata in `RoboFontSystemPack`.

---

## 5. Caches and invalidation

RoboText keeps **no** shaping cache in this build (`RoboTextCapabilities.shapingCache` describes the record's
readiness, not a cache that exists). The invalidation contract is 12.22's and lives in
`RoboTextEditInvalidation`, which is the interface a consumer redoes work from rather than a hint:

| Field | Meaning |
|---|---|
| `textRange` | the source scalars that actually changed |
| `analysisRange` | the paragraph-widened span a re-analysis must cover |
| `shapingRange` | the same span, plus what a shaping pass reads around it |
| `layoutRange` | the span a layout pass must redo |
| `requiresBidiReanalysis` | set when inserted **or removed** text carries a right-to-left scalar or a number (UAX #9's I1/I2) |
| `requiresFontReselection` | set when text changed at all |
| `requiresReshape` / `requiresRelayout` / `requiresRepaint` | the three passes, in order |

Correctness first: a one-scalar edit can change its neighbours' shaping, so the honest range is a paragraph; a
finer range later is a change of *range*, not of mechanism. A consumer that caches shaped runs keys them on the
inputs that affect shaping — text identity, range, face, size, weight, style, stretch, language, script,
direction, bidi state, writing mode, feature settings, variation state — and must never reuse an entry when one
of them differs.

A change that invalidates **downstream** only (font size, weight, style, stretch, letter spacing) invalidates
measurement, layout and paint but not the Unicode analysis, the segmentation or the runs; a change to the text
invalidates the lot from `analysisRange` down.

---

## 6. Error handling and the missing-glyph contract

| Input | Answer |
|---|---|
| a NULL where a pointer is required | `RoboInvalidArgument`, result NULL |
| an ill-formed UTF-8 byte in a *view* | U+FFFD, one per bad byte, advancing one byte — identical in measurement, rendering, selection and editing |
| an ill-formed scalar handed to `RoboTextBufferCreateFromScalars` | `RoboInvalidArgument` — a caller holding code points has already decided |
| a context with no face | `RoboNotSupported`, result NULL |
| a face whose `cmap` or metrics will not parse | `RoboError` (refused, not measured as zero) |
| a malformed optional table (`GSUB`/`GPOS`/`GDEF`) | refused by name (`RoboParseError`), or the lookup is skipped when skipping is safe — never a corrupted glyph buffer |
| an unmapped scalar | `.notdef`, an advance, a counted miss, and the source mapping intact |
| a script with no strategy | the `None` row: the text is not shaped into a wrong script |
| an unplaceable caret position | `RoboTextCaretGeometry.valid == 0` with zero spans — never a failure, never a guessed point |

The fallback ladder is `valid glyph → the chain's next face → `.notdef`` (`RoboTextFontSelection.h`), and
`FontSelection.complete` reports whether the chain covered the run rather than repairing it silently.

---

## 7. What a version bump means here

`RoboTextGetVersion` and `RoboTextCapabilities` are the honest record, and a later phase flips a flag **in the
same change** that adds the behaviour. The rule the version pair keeps is the other direction too: a phase that
changes no capability and no engine behaviour — 12.23's conformance corpus, 12.24's API freeze — moves neither.
RoboText is at `1.13.0`, set by 12.22; 12.24 removed a legacy path and added no capability, so it is unchanged
(`text.api.version_and_capabilities` reads each field).
