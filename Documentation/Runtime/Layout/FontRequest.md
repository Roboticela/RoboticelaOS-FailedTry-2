# The resolved font request (Phase 12.17, the CSS seam)

12.17 connects CSS font properties to the RoboText typography pipeline. This document covers the
**first, foundational half** that has landed: the CSS-side *resolved font request* — an ordered family
list plus the scalar settings — in `Runtime/Source/RoboLayout/RoboLayoutFontRequest.c`, public header
`Runtime/Include/RoboLayout/RoboLayoutFontRequest.h`.

The second half (per-run face selection through RoboText's selector, and per-element mixed-font runs) is
described in §5 as the remaining work.

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
LayoutFontRequest            ← this document
      │  families (ordered) + sizePx + weight + stretch + italic + direction
      ▼
(a font resolver selects a face per shaping run — RoboText's selector over the pack)
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

## 5. What remains (the phase's second half)

This landed half satisfies the *representation* half of 12.17. The wiring half — turning a request into
selected **faces** per shaping run and feeding them to measurement and paint — is not yet in place:

- a **system/generic family binding**: mapping `system-ui`/`ui-display`/`sans-serif`/`monospace` and a
  named family to a face in the system pack, centrally (a candidate home is `RoboTypography`, which
  already owns role → face and may depend on RoboFont);
- **per-run face selection**: the document's text pass currently measures and draws with one face
  (`DesktopDocumentFace`). Selecting a face per shaping run through 12.5's `RoboTextFontSelector` over a
  fallback chain is what produces mixed-font `GlyphRun`s and per-script fallback;
- **diagnostics** exposing requested vs. selected font, and the desktop/document integration and QEMU
  visual evidence the phase's §33–§34 ask for.

Until that lands, the request is the CSS-side answer and nothing consumes it yet beyond its own suite.

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

Run `Scripts/Test/TestLayout.ps1 -Target Host`.
