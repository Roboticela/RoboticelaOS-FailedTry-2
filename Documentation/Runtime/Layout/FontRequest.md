# The resolved font request, and the face it resolves to (Phase 12.17)

12.17 connects CSS font properties to the RoboText typography pipeline, and it does so in **two halves
that are now both in the tree**:

1. the CSS-side *resolved font request* — an ordered family list plus the scalar settings — in
   `Runtime/Source/RoboLayout/RoboLayoutFontRequest.c`, public header
   `Runtime/Include/RoboLayout/RoboLayoutFontRequest.h`; and
2. the *selection seam* — request in, one borrowed face out, through a provider the caller supplies — in
   `Runtime/Source/RoboLayout/RoboLayoutFontResolver.c`, public header
   `Runtime/Include/RoboLayout/RoboLayoutFontResolver.h`, with the one concrete provider above this module
   (`Runtime/Source/RoboDesktop/RoboDesktopFontProvider.c`).

The rule the whole phase turns on:

> **CSS chooses the requested typography; the font database chooses the actual font.**

Layout owns the *request* and the *seam*. It never owns the database: which face a family is stays in
RoboFont and the system pack, behind a callback. A build that installs no provider measures every run with
the one attached face, exactly as it did before the phase, and `LayoutCapabilities.fontRequest` says so.

---

## 1. Why the request is its own record

`RoboLayoutText.h`'s `LayoutTextStyle` is the **measurement seam**: four scalars (size, weight, an italic
flag, a direction) a measurer can be handed without knowing CSS. It deliberately does not carry
`font-family` — not because the family does not matter, but because

- a family is an ordered, variable-length list, and
- `LayoutTextStyle` is copied **by value** into every text run and, since 11.6/12.16, into a
  `PaintTextRun`. A pointer or an owned array in that record would turn a value type into a lifetime
  question and would break the by-value copy the paint record model relies on.

So the *selection* question gets its own record, and it is a value too: a bounded inline family list and
five scalars, with **no pointer in it**. A caller can hold one on its stack, copy it, and hand it to a
selector.

```text
CssComputedStyle
      │  font-family / font-size / font-weight / font-style / font-stretch / direction
      ▼
LayoutFontRequest            ← this document, §1–§4
      │  families (ordered) + sizePx + weight + stretch + italic + direction
      ▼
LayoutFontRequestSelect      ← this document, §5
      │  a LayoutFontProvider the caller installed (RoboDesktop's, over the registry + the pack)
      ▼
LayoutFontSelection          (face, familyIndex, fallback)
      │
      ▼
LayoutTextRun.face           (recorded once, so width and glyphs come from one selection)
      │
      ▼
RoboText measurement · RoboPaint's pen
```

## 2. What the request is, and what it is not

It **reads CSS and normalizes it**. It does not load a font, does not map a Unicode scalar to a glyph,
does not choose a fallback face and does not shape. Those belong to RoboText (12.5) and the font system
(Phase 5). A second implementation of any of them here would be the "second font system" the phase
forbids: `Documentation/Runtime/Text/FontSelection.md` / `FontFallback.md` and
`RoboFont`'s own `FontSelect`/`FontGenericResolver` remain the only ones.

## 3. The family list, kept ordered and classified

A CSS family list is ordered and its tail is usually a generic:

```css
font-family: "UI Text", "Inter", sans-serif;
```

Resolving that to one face here would destroy per-script fallback — the very thing 12.5's selector exists
to do. So the list is kept entry for entry:

| Entry | Represented as |
|---|---|
| a quoted or unquoted **name** (`Inter`, `Gill Sans`) | `LayoutFontFamilyNamed`, the spelling with quotes removed |
| a **keyword** (`serif`, `sans-serif`, `monospace`, `cursive`, `fantasy`, `system-ui`, `emoji`, `math`, `ui-serif`, `ui-sans-serif`, `ui-monospace`, `ui-rounded`) | `LayoutFontFamilyGeneric` holding a `FontGenericFamily` |

The keyword test is `FontGenericFamilyFromName`, RoboFont's own vocabulary reader, so this module never
hard-codes `sans-serif → Inter`. A generic stays a generic all the way down.

The parser (`LayoutFontFamilyListParse`) splits on top-level commas, honours single and double quotes (so
a comma inside a quoted name does not end an entry), trims ASCII whitespace, strips a surrounding quote
pair, and skips empty entries. It is **bounded**: past `RoboLayoutFontMaxFamilies` entries the rest are
dropped and the call still succeeds, so a pathological declaration cannot make layout fail. `length == 0`
reads the text as NUL-terminated (RoboFont's own convention).

*Known limitation:* the CSS **computed** value for `font-family` (9.8's `ComputedComputeFamily`) joins the
list into one string with `", "` separators and de-quotes each name, so a family whose own name contains a
comma is already indistinguishable from a separator by the time it reaches this seam. That is a 9.x
representation limit and is not worked around here.

## 4. The adapter, and the scalars it delegates

`LayoutFontRequestResolve(computed, out)` is the CSS adapter. It reads five properties and delegates the
scalar half to the measurer's own resolver so the two records cannot disagree:

| Property | Where it is read |
|---|---|
| `font-family` | the family list, via `LayoutFontFamilyListParse`; the initial list when absent |
| `font-size` | delegated to `LayoutTextStyleResolve` (10.3's one px-length reader) |
| `font-weight` | delegated — a NUMBER clamped to 1..1000, or the four keywords |
| `font-style` | delegated — `italic`/`oblique` set the flag |
| `direction` | delegated — `rtl` selects RTL |
| `font-stretch` | read best-effort here (§4.1) |

`computed` may be NULL, which yields the initial values rather than a failure — the reading every other
RoboLayout resolve door gives.

### 4.1 Initial values, in one place

The scalar defaults live in `RoboLayoutText.h` (`RoboLayoutTextInitialFontSizePx`, the two weights, LTR).
The family initial is spelled once in the adapter as a single **generic `serif`** — which is what
`RoboCSSPropertyRegistry.c` records as `font-family`'s own initial value for this build — and `stretch` is
`RoboFontStretchNormal`. A document that declares nothing therefore resolves to exactly what the registry
says a declaration-less element is; neither table invents a second answer.

### 4.2 `font-stretch` is best-effort, and named as such

The CSS layer does **not** yet register `font-stretch` as a computed property (the phase's §13 explicitly
allows deferring it). The adapter therefore reads a *recognizable* value — a PERCENTAGE in `[50, 200]` or
one of the nine keywords — and otherwise answers the initial `normal`. The integration path is defined;
the CSS registration is a later row.

### 4.3 One diagnostic flag

`familyUnresolved` is the one diagnostic on the record, and it exists so that **"the declaration was
unusable"** and **"the declaration was absent"** are tellable apart: both resolve to the initial list, and
a diagnostic that could not say which would hide a malformed stylesheet behind a default. `RoboLayout`'s
invalidation table already classifies every one of these properties (`font-family`, `font-size`,
`font-weight`, `font-style`, `font-stretch`) as `RoboLayoutStyleEffectLayout`, so a change to any of them
already invalidates text layout — the phase's "font property change invalidates layout" requirement is
satisfied by the existing classification.

## 5. The selection seam

`LayoutFontRequestSelect(request, provider, out, diag)` walks the request's family entries **in author
order** and asks the provider for each. The first entry that yields a face wins; the walk **stops there**,
which is why `LayoutFontSelection.familyIndex` and `fallback` are recorded rather than inferred — "which
family answered" and "was that the first" are the two facts a diagnostic or a cache key needs.

The provider is a **vtable of two fields** (`userdata` and a `resolveFamily` callback), the same shape
`LayoutTextMetrics` uses for measurement and `LayoutStyleSource` for computed styles. That is the whole of
the module's font abstraction: `RoboLayout` does not open a file, scan `/System/Fonts/`, parse a `cmap`,
walk coverage or hold a registry. A generic entry is handed over **as a generic** — a `RoboFontGenericFamily`
— so `sans-serif → Inter` is a mapping the database owns and this module never sees.

Three answers, and the third is the one that matters:

| Provider says | Meaning |
|---|---|
| a face | that family answered; record it and stop |
| nothing (`RoboOk`, `outFace == NULL`) | this database has no such family; try the next entry |
| a failure code | the database could not be consulted; propagate, do not read it as "no match" |

A NULL or unusable provider, or a request no entry answers, leaves the selection **unset** (`resolved ==
0`) and is not an error: the caller degrades to its own default face, which is exactly what a document
without a registry does.

### 5.1 One selection, threaded

The measurer resolves the request **once per run** and records the answer on the run
(`LayoutTextRun.face`, and `fontFallback`), and the paint pass draws with *that* face instead of resolving
again. The project's rule, applied: a fact a later layer needs is carried, not recomputed. It is what
makes a run's width and a run's glyphs come from one selection, and it is why
`PaintContextSetTextFace`'s single face is now only the *fallback* the recorder uses when a run carries
none.

The face is **borrowed** throughout — the caller owns whatever registry the provider selected out of, and
the run holds no reference it would have to release.

### 5.2 The one concrete provider

`RoboDesktopFontProvider` (`Runtime/Source/RoboDesktop/RoboDesktopFontProvider.c`) is the implementation
`RoboLayout` must not contain, and it is built from things the desktop already has:

| Entry kind | How it resolves |
|---|---|
| a **generic** (`sans-serif`, `system-ui`, `ui-monospace`, …) | the pack's configured generic mapping (`FontGenericResolver`, fed by `FontSystemPackApplyGenericConfig`), falling back to the pack's own role for that generic |
| the two **role names** a pack may spell (`ui-text`, `ui-display`) | `FontSystemPackResolveRole` — the same door 12.3's chrome roles use |
| a **named family** | `FontRegistryCollectFacesForFamily` + Phase 5's `FontSelect` |

It counts `resolved`/`unresolved` asks; `DesktopDocumentReport.fontFamiliesResolved` and
`…fontFamiliesUnresolved` publish them, so "did a document's `font-family` reach the font database?" is two
numbers on the document's own report.

### 5.3 What selection does *not* do

- **No per-scalar or per-script splitting.** One run, one face: the request carries the family list a
  later splitter would cut on, and 12.5's `RoboTextFontSelector` is what walks a fallback chain per script
  run. Selection here answers "the first family entry that exists", which is CSS's own answer for a list.
- **No coverage walk.** A face is not tested against the text; if it lacks a glyph, that is the text
  layer's miss to count (and a later phase's fallback to answer).
- **No synthesis.** A requested weight or slope the selected face does not have is *recorded*, not
  faked — the no-emboldening rule 12.3 states.
- **No `@font-face`, no web fonts.**

### 5.4 Diagnostics

Requested and selected are two records with two complete-or-absent writers: `LayoutFontRequestWrite`
(the ask: families, size, weight, stretch, italic, and whether the declaration was unusable) and
`LayoutFontSelectionWrite` (the answer: which family index, whether it was a fallback, the settings).
Together with the run's own `face`/`fontFallback` fields that is the phase's §29 "requested vs selected"
representation, without a second logging path.

### 5.5 Invalidation

Unchanged from §4.3 and sufficient: `RoboLayout`'s invalidation table already classifies `font-family`,
`font-size`, `font-weight`, `font-style` and `font-stretch` as layout-affecting, so a change to any of
them invalidates text layout — and therefore the request, the selection, the measurement and the paint.
No second invalidation path was added for a font change, which is the point.

## 6. Tests

`Tests/Layout/SuiteRoboLayoutFontRequest.c` (`layout.fontrequest.*`):

| Case | What it pins |
|---|---|
| `layout.fontrequest.init` | the initial values, including the `serif` initial family, and NULL-safety |
| `layout.fontrequest.family_parse` | commas, quotes, whitespace, empty entries, generic classification (every keyword), case folding, bounded overflow |
| `layout.fontrequest.family_equal` | case-insensitive names, order significance, named vs generic |
| `layout.fontrequest.resolve` | the five properties read from a real computed style; the scalars matching the measurer's resolver; an unusable family told apart from an absent one |
| `layout.fontrequest.stretch` | a keyword, a percentage, an out-of-range percentage and absence |
| `layout.fontrequest.equal` | the field-for-field rule the run-splitting walk will depend on |
| `layout.fontrequest.write` | the one-line writer, complete or absent |

`Tests/Layout/SuiteRoboLayoutFontSelect.c` (`layout.fontselect.*`, a **scripted** provider, so the seam is
proved without a font file):

| Case | What it pins |
|---|---|
| `layout.fontselect.provider` | the usable/unusable predicate, including NULL |
| `layout.fontselect.init` | an unset selection: no face, no family, the initial settings |
| `layout.fontselect.first_family` | the first entry answers, one ask, not a fallback |
| `layout.fontselect.fallback` | a miss then a hit: the second entry answers, `familyIndex == 1`, `fallback`, two asks |
| `layout.fontselect.generic` | a keyword reaches the provider **as a generic**, not as a name |
| `layout.fontselect.none` | nothing matches: unset, every entry still asked; a provider failure propagates rather than reading as "no match" |
| `layout.fontselect.settings` | weight/stretch/style carried into the selection |
| `layout.fontselect.equal_write` | the equality rule and the writer, including the unset spelling and the too-small buffer |

The document-level case is `Tests/Desktop/SuiteRoboDesktopDocument.c`'s
`desktop.document.font_families`: two paragraphs whose `font-family` declarations are a shipped name, an
unknown name, and a generic, built once over the real registry that the desktop's provider is made from.
It asserts the **exact** resolved/unresolved counts (2 and 1), and that the same document with no registry
consults nothing and still draws.

Run `Scripts/Test/TestLayout.ps1 -Target Host`, then `Scripts/Test/TestDesktop.ps1 -Target Host`.

## 7. What still belongs to another phase

Selection here is one face for one run, resolved from a family list. Named features a later row owns: the
system/generic **role vocabulary** beyond the two role spellings a pack may use (`ui-text`/`ui-display`
already resolve through the pack; `system-ui`, `ui-monospace` and the rest resolve through the configured
generic mapping), per-script **fallback chains** and mixed-font runs (12.5's selector, driven from the list
this record preserves), `@font-face` and web fonts, and the QEMU visual evidence. The recorded face on the
run is what those will thread through; nothing needs to be re-derived.
