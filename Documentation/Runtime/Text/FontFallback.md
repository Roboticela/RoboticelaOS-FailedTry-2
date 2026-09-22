# RoboText font fallback (Phase 12.5)

Fallback is the one part of font integration that is **policy**, and policy belongs to the layer that
consumes it. RoboText therefore owns the *decision*; it does not own discovery, and it does not name a
family string to find one.

## 1. The chain is data

```
RoboTextFontFallback
    entries[0] … entries[n-1]        borrowed, ordered by the caller
```

`RoboTextFontSelectorSetFallback` takes that order verbatim. Selection tries the primary, then each entry in
turn, and answers with the first that has usable coverage — so **changing the fallback is changing the
array**, not editing a table inside RoboText. An empty chain is legal and means "primary or nothing", which
is exactly the 12.4 behaviour expressed as data.

The chain is not hard-coded anywhere, and there is no language → font database here: a language-preferred
order is the caller's (or a later phase's) to build, because it is a database and RoboText keeps none.

## 2. Where each stage lives

| Stage (conceptual) | Who supplies it |
|---|---|
| Requested family / weight / style / stretch | the **primary `RoboTextFontContext`** — already resolved by whoever built it (12.3's role resolver, a stylesheet, a test) |
| UI-text fallback | chain entries, in order |
| System Unicode fallback | chain entries, in order |
| Specialized-script fallback | chain entries, in order |

RoboText does not distinguish these stages; it only walks the order and records which entry answered
(`candidateIndex`). That is deliberate: the stage names are a *way of building* the chain, and a text layer
that re-derived them would be a second font database.

## 3. Coverage is a hard gate

A candidate is eligible **only** if its face has a usable glyph for the code point. Nothing is selected
merely because it is installed. When nothing covers the text, the honest result is
`RoboTextFontSourceNone` with no face — not the primary's `.notdef` masquerading as a selection, and not the
first-installed face.

## 4. Coherent sequences, and combining marks

Fallback must operate on sequences, not single scalars, or a base character and its combining mark can end
up in different faces. Two mechanisms enforce that:

- `RoboTextFontSelectorSelectRange`/`…SelectRangeView` decide per **run** (all scalars, else the base);
- the analysis pass **copies the base's font onto a combining mark**, so a mark never triggers its own
  selection even when the selector is asked per scalar.

`"e" + U+0301` is one run; `"e" + U+E0100` (a mark no installed face carries) is still one run, with the
base's face.

## 5. Determinism

Given the same text, the same selector configuration and the same font database, the chain produces the same
runs, the same font identities and the same provenance — field for field, across independently created
selectors. `RoboTextResolvedFontEqual`/`…Compare` are the identity comparison a visual-regression check
needs. This is pinned by `text.fontselection.determinism` and `text.analysis.empty_refusals_determinism`.

## 6. Current limitation: the shipped pack has one family

`/System/Fonts/` ships **Inter** alone. Consequences for this phase, stated plainly:

- a Latin-only primary genuinely fails to cover Han, Hebrew, Arabic, Devanagari and emoji, and the analysis
  reports those runs as `RoboTextFontSourceNone` rather than inventing a face;
- the suite therefore exercises the **fall-through path** with a no-face primary plus a one-entry chain, which
  is the same code the multi-family case would run;
- when a CJK or emoji family is added to the pack and its manifest, a real mixed-script run appears by adding
  it to a chain — no RoboText change is required.

Adding a fallback family is a **pack** change (`System/Fonts/`, `system-fonts.manifest`, a licence row in
`Documentation/Legal/FontLicenses.md`, and the image build's completeness check), not a text-layer change.

## 7. Tests

`text.fontselection.fallback`, `text.fontselection.range` and `text.analysis.mixed_scripts` are the cases to
read; `text.fontselection.fuzz` runs the selector over arbitrary scalars with no face.
