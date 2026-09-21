# UTF-8 & Unicode contract (Goal 4.27)

Applies to all frozen RoboI18N public APIs unless a function documents otherwise.

## Inputs

* Text parameters are **UTF-8** byte sequences with explicit `len` (not requiring NUL
  unless documented).
* **Strict** parsing APIs reject ill-formed UTF-8 (`RoboParseError` / Unicode STRICT).
* Replacement mode is used only where Unicode APIs expose `RoboUnicodeReplace`.

## Lengths & offsets

* `size_t` lengths and search hit ranges are **byte offsets** into the UTF-8 buffer.
* Code-point or grapheme counts are never implied by `len`.
* Search hits: `[start, end)` are source byte ranges; with `grapheme_safe`,
  `[grapheme_start, grapheme_end)` expands to cluster boundaries without corrupting
  the semantic range.

## Normalization

* Formatters do **not** silently NFC/NFD-normalize entire inputs unless the
  subsystem documents it (e.g. search CANONICAL mode, collation).
* Transliteration may apply documented normalization stages; search does **not**
  auto-transliterate.

## Graphemes / emoji / bidi

* Grapheme-safe search is opt-in.
* Emoji ZWJ / VS sequences are valid UTF-8 inputs; shaping/fonts are out of Phase 4 scope.
* Bidi: formatters emit **logical** order; visual reordering is later (RoboText).

## Error positions

* Parsers that report `error_offset` / `consumed` use **byte** indices into the
  original UTF-8 input.
