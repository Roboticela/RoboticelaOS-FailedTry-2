# RoboText ↔ Unicode integration (Phase 12.5)

This is the contract for how the text layer consumes Unicode. It is deliberately short, because the
integration's whole design is **one owner per fact**:

| Fact | Owner | RoboText's door |
|---|---|---|
| Encoding, UTF-8 decode/encode | **RoboUnicode** (Phase 4) | `RoboTextUnicodeDecoder`, `RoboTextUnicodeDecodeAll` |
| General category, script, bidi class, whitespace, combining class | **RoboUnicode** (`RoboUcdData.h`) | `RoboTextUnicodeGeneralCategory`, `…Script`, `…BidiClass`, `…IsWhitespace`, `…CombiningClass` |
| Code-point / scalar validation | **RoboText** (a vocabulary, not a table) | `RoboTextCodePoint*` |
| Combining-mark *classification* | **RoboText** (a category group) | `RoboTextUnicodeIsCombiningMark` |
| `Script_Extensions` | **RoboUnicode** (`UcdHasScript`, `UcdScriptExtensions`) | `RoboTextUnicodeScriptInfoOf`, `…ScriptExtensions`, `…ScriptExtensionsInclude`, `RoboTextUnicodeAsciiScript` |
| Script-run segmentation (Common/Inherited neutrality) | **RoboText** | `RoboTextScriptRunsResolve` — contract: [ScriptRuns.md](ScriptRuns.md) |
| Direction **classification** (not resolution) | **RoboText** | `RoboTextUnicodeStrongDirection`, `…BaseDirection`, `…ClassifyDirection` |
| The bidirectional algorithm, reordering | **Phase 12.8** | none — `RoboTextGetCapabilities().bidiAlgorithm == 0` |

RoboText keeps **no Unicode table** and does not regenerate one. `RoboTextUnicodeDataVersion()` returns
RoboUnicode's own pin (`UnicodeUcdVersionString()`), so the two cannot claim different versions.

---

## 1. Code points

`RoboTextCodePoint` is a `uint32_t` in `0..U+10FFFF` — the code-point range, which **includes** the
surrogates. Unicode distinguishes a *code point* from a *scalar value* (a code point that is not a
surrogate); UTF-8 can only carry scalars, but a caller can build a code point from a UTF-16 unit, an escape
or its own decoder, and must be able to ask which it has:

```
RoboTextCodePointIsValid     0..10FFFF        (surrogates allowed)
RoboTextCodePointIsScalar    valid, not a surrogate
RoboTextCodePointIsHigh/LowSurrogate
RoboTextCodePointIsNoncharacter
RoboTextCodePointToScalar    the value, or U+FFFD when it is not a scalar
```

The surrogate range is rejected as a scalar even though UTF-8 never produces one. A property door handed a
non-scalar answers without reading a table (`Cn`, script `Unknown`, not a mark, not whitespace).

## 2. Invalid UTF-8 policy

**Every ill-formed byte becomes exactly one U+FFFD, the decoder advances one byte, and it continues.**
This is `RoboUnicodeReplace`'s policy, unchanged and unwrapped:

- an unterminated multi-byte tail becomes one replacement **per byte**;
- a lead byte with a bad continuation byte becomes one replacement, followed by whatever the next byte is;
- `0xFF 0x41` is `U+FFFD` then `A`.

The policy is identical in measurement, rendering, selection and any future editing path, because it is one
function. `RoboTextUnicodeDecoderLastWasReplaced` distinguishes a genuine U+FFFD from a replacement — the
same character, two different facts — by re-running the decoder in strict mode *only* when a U+FFFD came
back.

## 3. The decoder

`RoboTextUnicodeDecoder` is a value over a borrowed `RoboTextView`: `HasNext`, `Peek`, `Next`,
`Index` (the scalar index the next `Next()` returns), `LastRange`, `LastWasReplaced`. It allocates nothing,
so the several consumers of one text (segmentation, a diagnostic, a future measurer) each decode the same
bytes rather than each keeping a copy.

`RoboTextUnicodeDecodeAll` is the module's one "decode everything" door. `RoboTextRuns.c` and
`RoboTextAnalysis.c` both use it, so a document's text cannot be decoded two slightly different ways. It
walks twice (count, then allocate and fill) rather than growing an array — a document-sized text must not
be quadratic.

## 4. Script resolution

`RoboTextScriptRunsResolve` (implemented in `RoboTextScriptResolver.c`) is the **one** implementation of the
neutrality rule. 12.5's form of it was a single carry; 12.6 replaced that with a two-sided rule that also
consults `Script_Extensions`. Its contract lives in [ScriptRuns.md](ScriptRuns.md); in one line:

> a neutral scalar takes the script both of its neighbouring anchors agree on; failing that, a combining
> mark takes its base's; failing that, a script that lists it takes it; failing that, the left anchor's;
> failing that, the right's; failing that, it stays `Common`.

One forward pass with a deferred neutral block, allocating nothing. `"abc 123 def"` is one script run;
`"  123 abc"` is one run starting at scalar 0; `"123 456"` is one `Common` run (nothing resolved, so nothing
is claimed). A `scriptHint` in `RoboTextShapeOptions` classifies every scalar as the hinted script, digits
and spaces included.

The run set and the analysis layer both call this function — the run set for its published `RoboTextScriptRun`
array and its per-scalar array, the analysis for its runs — so they cannot disagree about a boundary, and the
analysis cuts its runs from this answer rather than segmenting again.

### The ASCII path

ASCII is answered from the block instead of from the tables, and the three shortcuts are all
`RoboTextUnicodeScript`-level facts rather than resolver-local ones:

- `RoboTextUnicodeAsciiScript` — `A`–`Z`/`a`–`z` are `Latn`, every other code point below U+0080 is `Zyyy`.
- `RoboTextUnicodeScriptExtensionsInclude` — for ASCII the set is the defaulted singleton, so membership is
  equality.
- `RoboTextUnicodeIsCombiningMark` — nothing below U+0300 is a mark.

`text.script.ascii_fast_path` proves each against the tables over the whole range it covers, so the
shortcut cannot silently drift from the data it shortcuts.

## 5. Combining marks and direction

`RoboTextUnicodeIsCombiningMark` is `Mn` / `Mc` / `Me`. Its consumers are the font pass (a mark keeps its
base's face — see [FontIntegration](FontIntegration.md)) and, later, shaping.

Direction is **classified, never resolved**: `R`/`AL` are RTL, `L` is LTR, everything else takes the
paragraph base — the caller's declared base, else the text's first strong scalar, else `Unknown` (a real
state for `"123 456"`, not an LTR claim). The UBA is Phase 12.8.

## 6. Tests

`Tests/Text/SuiteRoboTextUnicode.c`, filter `text.unicode.*`:

| Case | What it pins |
|---|---|
| `code_points` | validity / scalar / surrogate / noncharacter / `ToScalar` |
| `properties` | category groups, combining marks, whitespace, control, format, assignment |
| `data_version` | the version **is** RoboUnicode's |
| `decoder` | ASCII + 2/3/4-byte scalars, peek/next agreement, byte ranges, index, exhaustion |
| `decoder_invalid` | the replace policy, byte by byte, and genuine U+FFFD vs replacement |
| `decode_all` | the shared decode door, empty text, refusals |
| `scripts` | Latin, Cyrillic, Greek, Arabic, Hebrew, Devanagari, Han, Hiragana, Katakana, Hangul, Inherited |
| `script_runs` | neutrality, leading neutral, mixed scripts, hints, capacity, empty text |
| `direction` | strong classes, base direction, classification |
| `fuzz` | arbitrary bytes never spin or overrun |
