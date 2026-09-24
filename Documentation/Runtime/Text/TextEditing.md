# Text editing (Phase 12.22)

> **Editing operates on logical source text; shaping and rendering are downstream consequences of text
> mutations.**

This is the phase's contract, and it is the first phase in the block whose *consumer* is above RoboText rather
than below it. Everything 12.4–12.21 built describes text; this describes **changing** it.

```text
RoboInput / future IME / future DOM editing
        │  TextEditCommand
        ▼
    RoboTextEditor ──▶ RoboTextEditOperation ──▶ RoboTextEditInvalidation
        │
   RoboTextBuffer  (the logical text)
        │
  Unicode · script · bidi · font selection
        │
      RoboText shaping
        │
    RoboTextGlyphRunList ──▶ TextMeasurement ──▶ TextLayout ──▶ RoboTextSelectionMap
```

One implementation, one file: `Runtime/Source/RoboText/RoboTextEditing.c` with the public contract
`Runtime/Include/RoboText/RoboTextEditing.h`. It **shapes nothing and measures nothing**: it owns one
`RoboTextBuffer`, one `RoboTextSelection` and one `RoboTextCaret`, and every operation it can perform is one
replacement of a source range. A glyph index appears nowhere in it, and no query in it reads a run list —
except the one that has to, which is the boundary resolver a caller *installs* (see §6).

---

## 1. Where the code lives

| Piece | File |
|---|---|
| The editor, the operation record, the invalidation, the boundaries, composition, transactions, transformation, the dumps and the fuzz entry | `Runtime/Source/RoboText/RoboTextEditing.c`, public `Runtime/Include/RoboText/RoboTextEditing.h` |
| The text it edits | `Runtime/Source/RoboText/RoboTextBuffer.c` (`RoboTextBufferRemove` then `RoboTextBufferInsert` — the one mutation, written once) |
| Positions, ranges, affinity, selections | `Runtime/Source/RoboText/RoboTextSelection.c` (12.21's vocabulary, reused rather than re-declared) |
| The cluster boundaries an edit may land on | `RoboTextGlyphRunCluster*` (12.14) through `RoboTextEditBoundariesFromRuns` |
| Characters' properties, for the character boundary | `RoboTextUnicodeGeneralCategory` (12.5) — this file keeps no Unicode table |
| The safety of a complex-script boundary | `RoboTextComplexIsSafeBoundary` (12.19) — the module below already answers it |

---

## 2. One mutation, and everything else written in terms of it

`RoboTextEditorReplace(editor, range, text, length, out, diag)` is the only door that changes the text. Every
high-level operation is that door with a computed range (12.22.7):

| Operation | Range it replaces |
|---|---|
| `Insert(text)` | the **selection** (the caret when it is collapsed) |
| `ReplaceSelection(text)` | the selection |
| `DeleteBackward()` | `previous boundary .. caret`, or the selection when it is not collapsed |
| `DeleteForward()` | `caret .. next boundary`, or the selection |
| `Command(InsertText/Paste/Cut)`, … | one of the above |

There is no second mutation path, and that is what makes a transaction's cancel a *replace* rather than a
replay of half a dozen subtly different edits.

The edit is **atomic**: the new text is built in a clone and installed only when every step has succeeded, so
a refusal leaves the editor exactly as it was and a caller holding a `RoboTextBuffer *` from before keeps a
valid one.

## 3. The bytes are checked; the buffer is not a validator

An insertion *by a caller* is validated — an ill-formed sequence is `RoboInvalidArgument`, not silently
repaired, because a text a caller built is a caller's bug to hear about. A `RoboTextBuffer` *given* the bytes
is not: it is a byte store, and rewriting a caller's text behind their back is the same silent repair refused
one door over. The two halves are therefore different doors, and reading either decodes with the module's own
REPLACE policy (one U+FFFD per ill-formed byte), so an editor over such a buffer is usable rather than wedged.

`RoboTextEditorInsert(editor, NULL, 0)` is legal and inserts nothing; `NULL` with a non-zero length is
`RoboInvalidArgument`.

## 4. The record undo will need

`RoboTextEditOperation` keeps **both** texts, **both** selections and **both** carets:

```text
replacedRange      in the OLD text's coordinates
deleted  inserted  owned buffers; never NULL; empty for the other half of a pure edit
selectionBefore/After  anchor and focus on both sides
caretBefore/After      position AND affinity on both sides
```

`deleted` is kept so a history can put it back without asking a layout what used to be there — which it could
not answer, the old text being gone by then. The carets are kept for the same reason: reconstructing where a
caret was from glyph widths would be deriving logical state from geometry, which this module's whole design
refuses. `RoboTextEditOperationClone` is the door a caller's own history uses, because an operation that came
out of a result is owned by that result.

**The history itself is not built** (12.22.27): `RoboTextEditCommandUndo`/`Redo` are `RoboNotSupported` by
name, and `RoboTextCapabilities::undoHistory` is `0`. Answering "nothing happened" would be worse than saying
no.

## 5. The result

`RoboTextEditResult` is the status, the operation, the affected range (in the **new** text), the selection and
caret afterwards, and the invalidation. A no-op is `RoboOk` with `changed` 0 — a backspace at the start of the
text is a legitimate nothing, not an error, and reporting it as one would make every caller special-case it. A
malformed call is `RoboInvalidArgument`; work this build does not do is `RoboNotSupported`; the three are
different answers and a caller can tell them apart.

## 6. Boundaries: what a delete is allowed to cut

| Boundary | Answered? | What it is |
|---|---|---|
| `Scalar` | yes | the index space's own step — never splits a UTF-8 sequence |
| `Character` | yes | one scalar, extended over the **combining marks that follow a base** |
| `Cluster` | yes, **with a resolver** | a shaped cluster, from a run list the caller supplies |
| `Word` | **refused** | UAX #29 word segmentation; nothing in this build does it |
| `LineStart` / `LineEnd` | **refused** | they need a layout, which this layer does not have |

The character rule is asymmetric on purpose: step back one scalar, then keep stepping back while the scalar
landed on is a combining mark. A mark belongs to the character before it (12.14's cluster rule), so the
boundary before a base is before its marks too — which is what makes `e` and its three accents **one**
backspace, and a forward delete the mirror image.

`Word` and the two line boundaries are refused **by name** rather than approximated with `position ± 1`: a
caller that asked for words and got characters would delete half a conjunct and never know. A position outside
the text is `RoboInvalidArgument`; past the last scalar a step stops at the text's end.

### 6.1 The cluster resolver

`RoboTextEditBoundariesFromRuns(list)` flattens a **shaped run list's** clusters into one ascending array, and
`RoboTextEditorSetBoundaries` installs it. From then on `RoboTextEditorBoundary` answers `Cluster` and every
delete and movement uses it. Without one, cluster boundaries are `RoboNotSupported` — not a quiet fallback to
the character rule.

The list is **borrowed** and the resolver does not own it. The one contract this layer cannot check is that the
boundaries were built over the editor's *current* text; a resolver over other text would answer about the
wrong scalars, so it is stated here rather than discovered by a test (and no test in the suite does it).

## 7. Complex scripts: the shaped cluster is the unit

Editing Arabic is editing **text**:

| Text | The shaped run's clusters | The editor's unit |
|---|---|---|
| `ببب` (three dual-joining letters) | 3 | one letter per backspace |
| `لا` (lam + alef, one ligature glyph) | **1** over **2** scalars | both letters, at once |
| `क` `्` `ष` (a conjunct) | **1** over **3** scalars | the whole conjunct |

A one-letter edit does not touch a contextual glyph: it changes the source and the next shaping call answers
differently, which is why the suite proves it by **shaping the editor's own bytes and comparing** them with the
shape of the text written directly (`ConfResultsEqual`) rather than by remembering an id.

The two layers answer two different questions, and `لا` is the case that shows it: 12.19's complex clusters
group a joining *sequence*, so the two letters are two one-scalar clusters and a **line** may break between
them; the run's cluster is the ligature glyph the font's bytes state, which is one unit of shaped output. The
editor reads the run's answer, because an edit that removed one letter of a ligature would cut it in half —
while a line breaker is legitimately allowed to break there. `RoboTextComplexIsSafeBoundary` is what a
*line breaker* asks, and it is asserted as such.

## 8. Bidi and writing mode: one editor, not two

There is no vertical editor and no right-to-left editor. The same `RoboTextEditor`, the same operation record,
the same invalidation and the same resolver serve every writing mode, because the writing mode is data on the
shape options and on every run (12.20) and the editor never sees either: it changes source scalars, and where
they land is 12.21's map over the next shaping call. A column's clusters are in **inline (source) order**
whatever the block axis does, so a backspace in a `vertical-rl` column removes the letter before it in the
text.

## 9. Invalidation: what a consumer must redo

`RoboTextEditInvalidation` answers with five ranges and seven flags. The ranges are **paragraph-widened**:
Unicode analysis, script resolution and the bidirectional algorithm are all at least paragraph-scoped, so the
smallest range that is provably sufficient without a shaping context this layer does not have is the paragraph
containing the edit's seam.

```text
textRange      the edit's own range, in the NEW text
analysisRange  the range whose Unicode/script/direction analysis is stale
shapingRange   the range that must be re-shaped
layoutRange    the range whose line breaking and geometry are stale
paintRange     the range that must be repainted
```

Two flags are **discriminating** rather than always on, and they are the reason the flags are separate from
the ranges:

- `requiresBidiReanalysis` — set only when the text inserted or removed carries a scalar whose level a
  paragraph does not simply inherit from its base (a right-to-left scalar, or a **number**, which UAX #9's
  I1/I2 give a level the base does not decide). Inserting Arabic into a Latin paragraph can split a level run;
  inserting Latin into one cannot. `x` and `ب` are the pair the suite proves it with.
- `requiresFontReselection` — set only when text was inserted or deleted at all: a scalar the previous shaping
  never saw can be covered by no face the previous selection used, and this layer has no font database to ask.

A no-op's record is the empty one: every range `[0,0)`, every flag `0`, `changed` 0.

**Correctness first, and a finer range later.** A one-scalar edit *can* change its neighbours' shaping (an `fi`
that becomes a ligature, an Arabic letter that changes form, a Devanagari conjunct that reforms), and the
honest answer available here is a paragraph. A later, finer contract would be a change of **range**, not of
behaviour — which is exactly what 12.22.30 asks for: "the initial implementation may conservatively invalidate
a complete shaping paragraph".

## 10. Composition (IME)

`RoboTextComposition` is a range in the **source text** plus the text currently in it, and it is stored
`selectedRange`/`clauseRange` **relative to the composition's start**, because an IME reports clause boundaries
relative to the composition and storing them absolutely would make every one of them stale the moment the
composition grew.

The composing text **is** text — it lives in the editor's buffer, so the caret, the selection and every query
about the text work during composition, and a reader can put a caret in it. What keeps it *distinguishable*
from committed text is that the editor also holds `textBefore`: the slice the composition replaced.

- `UpdateComposition` starts one over the **selection** (so an IME replacing a selected word starts by
  replacing it) or replaces the running one's own range, so an IME that updates per keystroke produces one
  result per update and one range that grows.
- `CommitComposition` publishes the composition as **one** operation — the range it began over, the text that
  was there, the text that is there now — and asks only for a **repaint**, because the text did not change:
  what stopped being true is the composition *boundary*, which a renderer drawing an underline has to redraw.
- `CancelComposition` is a **replace** of the pre-composition slice with the selection and caret from before
  the composition began. A five-keypress composition therefore cancels to the word it started by replacing,
  not to a collapsed caret at its own end.

The `TextRunDecoration` enum of 12.22.24 is deliberately **not** added: nothing in this build renders a
composition, and a decoration nothing consumes would be a flag claiming a capability the code does not have.

## 11. Transactions

`RoboTextEditTransaction` is several edits that appear as **one** logical mutation. The members apply as they
are added — so every intermediate state is a real, valid text a caller could query — and then:

- `Commit` publishes a **single** result whose operation spans the whole transaction (the union of the members'
  ranges in the *original* text's coordinates, the snapshot's slice as `deleted`, the current text's as
  `inserted`) and whose invalidation is the union of its members';
- `Cancel` puts the text back in **one replace** from the snapshot, and the selection and caret with it. Not a
  reverse replay: each member's range is in the coordinates of its own moment, and reconstructing the sequence
  backwards would have to be right about every one of them.

The member operations stay readable (`…Count`/`…OperationAt`), which is what a history that wants to undo one
keystroke of a paste needs, and the spanning operation is what it wants for the whole of it. A transaction
neither committed nor cancelled leaves its edits in place — stated, because the alternative would be a silent
undo.

## 12. Transformation

`RoboTextPositionTransformAfterEdit` maps a position that was valid **before** an operation to where it is
afterwards, in logical source coordinates:

| Position | After the edit |
|---|---|
| before `replacedRange` | unchanged |
| inside it, or exactly at its start | the **end** of the inserted text |
| exactly at its **end** | also the end of the inserted text — a boundary is owned by the range before it |
| after it | shifted by `inserted − deleted` |

That third row is the one that is a decision rather than an arithmetic: a selection that ended exactly where
the edit began follows the text it selected. `RoboTextRangeTransformAfterEdit` applies the same rule to both
ends, and both rules are monotone, so a valid range stays valid and never inverts — nothing is "repaired"
here, because repairing it would hide a broken rule. `RoboTextPositionTransformBeforeEdit` is the inverse,
which is what a range somebody *else* is holding across an edit needs.

This is groundwork for annotations, multiple selections, composition ranges and accessibility ranges; nothing
in this build consumes it yet beyond the doors themselves.

## 13. Selection and caret preservation

After every operation the selection is updated **logically**, in the one place the text changes:

- an insertion with a collapsed selection leaves the caret after the inserted text;
- a replacement of a selected range collapses the selection after the replacement;
- a refused or no-op edit leaves the selection unchanged.

`RoboTextEditorSetSelection` **clamps** rather than refuses: an editor that has just been told the text shrank
legitimately holds a selection from before that, and the contract is "the nearest position this text has". An
inverted pair stays inverted — clamping is not reordering, and `RoboTextSelectionRange` is the door that
orders. `RoboTextEditorSetCaret` refuses an affinity that is not a name rather than storing it.

The caret itself is `RoboTextCaret`: a position, an affinity and two flags (`visible`, `enabled`). There is
deliberately **no geometry** in it. A caret's visual form is 12.21's query against a selection map, which needs
the placements a layout pass supplies; the editor has no layout and must not invent coordinates it cannot see.

## 14. Diagnostics

Four complete-or-absent blocks, each reporting the length a complete line needs and leaving the buffer empty
when it does not fit: `RoboTextEditorWrite` (counts, selection, caret, boundary, composition state and the
edit/no-op counters), `RoboTextEditOperationWrite` (`replace 6..11 deleted 5B inserted 8B caret 0->14
downstream->downstream`), `RoboTextEditInvalidationWrite` (five ranges and seven named flags) and
`RoboTextCompositionWrite` (`none` when it is not running). `RoboTextEditingFuzz` drives an editor from
arbitrary bytes — random ranges, insertions the data decodes to, deletes, composition updates, commits,
cancels and transforms — with one invariant: whatever the bytes are, the text is valid UTF-8, every position
is inside it, the module itself never inverts the selection, buffers do not leak and no loop runs forever. A
zero-length call is valid; `NULL` with a size is `RoboInvalidArgument`.

## 15. Tests

`Scripts/Test/TestRoboText.ps1 -Target Host` — `text.*`, **322 cases**, of which sixteen are `text.editing.*`
in `Tests/Text/SuiteRoboTextEditing.c` over the **12.18 conformance fixture** (Latin, Greek, Cyrillic, a
zero-advance combining mark and the `liga`/`dlig`/`rlig` ligature set), the **12.19 complex fixture** (Arabic's
joining and lam/alef, Devanagari's conjunct) and the **12.20 vertical fixture** (`あ` + U+3099 in a column).

| Case | The claim |
|---|---|
| `text.editing.vocabulary` | the command and boundary names, the caret and invalidation values, and the three refusals |
| `text.editing.basic` | insert at the beginning, the middle and the end; replace; the two deletes; the whole document; the misuses; clamping |
| `text.editing.commands` | the command layer: the refusals, movements, extending selections, copy/cut/paste, select-all |
| `text.editing.utf8` | one, two, three and four-byte scalars deleted one scalar at a time, and an ill-formed insertion refused |
| `text.editing.marks` | `e` + two accents as **one** character unit, and the marks-only case |
| `text.editing.ligature` | `ffi` as one cluster with a resolver and three characters without; the typed path shapes like the text |
| `text.editing.arabic` | the lam/alef ligature and a joining sequence reshaped after a delete, with the complex module's own safe-boundary answer beside it |
| `text.editing.indic` | the conjunct as one unit; the half form after one scalar; the conjunct reformed by two insertions |
| `text.editing.bidi` | the flag on for Arabic and a digit, off for Latin, and the empty record for a no-op |
| `text.editing.vertical` | the same editor over a column: a base and its mark one unit, three letters three, all in source order |
| `text.editing.composition` | start over a selection, update, cancel (restoring the text **and** the selection), commit |
| `text.editing.transaction` | members, the spanning commit, the restoring cancel, and the identity |
| `text.editing.transform` | the four position rules, their inverse, ranges, and a round trip |
| `text.editing.invalidation` | the paragraph-widened range and its end at the terminator, the flags, and the block |
| `text.editing.diagnostics` | the four blocks, complete or absent, and the counters |
| `text.editing.fuzz` | every prefix and sweep of arbitrary bytes |

A 12.22 change is not done until `TestRoboText.ps1` **and** `TestLayout`/`TestPaint`/`TestGraphics`/
`TestRasterizer`/`TestFont`/`TestDesktop`/`TestRoboTextConformance` (`-Target Host`) pass. There is no guest
half for this phase: the document path carries no editor (no input, no focus, no IME), so a guest checkpoint
would be a second render of something the host already proves — and the phase's own acceptance list has no
QEMU criterion, unlike 12.20's and 12.21's.

## 16. Capabilities

12.22 sets, field by field, in `RoboTextVersion.h`/`.c` and read one by one in
`Tests/Text/SuiteRoboTextConformance.c`: `textEditing`, `editOperations`, `editingBoundaries`,
`clusterBoundaries`, `editInvalidation`, `editTransactions`, `compositionState`, `editTransformations`,
`editingDiagnostics`, `editingFuzzing`. The module moves to **`1.13.0`** — a minor move, because nothing a
caller already used changed shape or signature: the editor's state is 12.4's buffer plus 12.21's selection and
caret, and the records it adds are new types in a new header.

Three flags are **`0`** and asserted as such: `undoHistory` (the record is here, the manager is 12.23's),
`wordBoundaries` (UAX #29), `lineBoundaries` (they need a layout).

## 17. What this build does not claim

Named rather than implied, and each one is a row of the phase plan rather than an oversight:

- **`RoboInput`, keyboard scan codes, mouse events, touch** — the command layer is the boundary they will
  call, and nothing here maps a key to a command.
- **A clipboard backend.** `RoboTextEditorSelectedText` and `ReplaceSelection` are the two doors it needs;
  the service itself, and the platform, are not built.
- **An undo manager.** See §4.
- **Rich text, spell checking, autocorrect, word prediction, DOM editing, `contenteditable`, accessibility
  editing, drag-and-drop.**
- **Word, grapheme and line navigation.** `MoveUp`/`MoveDown`/`MoveToLineStart`/`MoveToLineEnd` need a layout
  and are refused; word boundaries need UAX #29. Character and cluster movement is what exists.
- **Incremental reshaping.** An edit currently asks for a paragraph, not a run (`§9`), and it rebuilds
  analysis, shaping and layout from the text rather than splicing into a cache. The architecture permits the
  finer contract; the invalidation ranges are the seam it would change.
- **Composition rendering.** §10.
- **IME, CJK/Arabic/Indic input methods, dead keys.** The composition state is the contract they will use.
