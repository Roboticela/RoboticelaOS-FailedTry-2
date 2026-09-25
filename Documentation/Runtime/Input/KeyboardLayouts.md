# Keyboard layouts and dead keys

`Runtime/Include/RoboInput/RoboInputLayout.h`, `Runtime/Source/RoboInput/RoboInputLayout.c` — Phase 13.9/13.10.

A layout turns a **position** into a **meaning**. It produces characters and stops there: it does not shape,
does not reorder, does not know what a grapheme is, and never touches a text buffer. What it produces is what a
`TEXT_INPUT` event carries (`TextInput.md`), and RoboText shapes it afterwards.

---

## 1. Four levels, and the two rules people get wrong

```c
InputLayoutLevel InputLayoutLevelFor(InputModifiers modifiers, int cased);
```

| Level | Selected by |
|---|---|
| `BASE` | nothing a layout cares about |
| `SHIFT` | shift down |
| `ALTGR` | `AltRight`, **or** `Ctrl` with `Alt` |
| `SHIFT_ALTGR` | both |

**AltGr has two spellings** (`AltRight`, or `Ctrl+Alt`), because a platform adapter cannot know which one the
hardware reports and a table that accepted only one would make every third-level character unreachable on half
the world's keyboards. A bare **left Alt is not AltGr**: it is a menu accelerator. And
`Ctrl`-without-`Alt` is a **control chord**, which is a different question from "which level?" — a chord
produces *no* text, and `InputModifiersIsControlChord` is the door that says so, exposed so a shortcut layer
and the text layer cannot disagree.

**Caps lock is not a level.** It inverts the shift level for a **cased** key and does nothing to an uncased one —
which is precisely why `Shift+1` is still `!` with caps lock on. A table that made caps lock a fifth level would
have to list every punctuation key twice to say "not this one", so instead a mapping carries
`RoboInputLayoutMappingFlagCased`, set by the builder for letters and clear for punctuation.

---

## 2. The table

```c
typedef struct InputLayoutMapping {
    PhysicalKey physical;      /* the position */
    InputLayoutLevel level;    /* which of the four */
    LogicalKey logical;        /* the interpreted identity, for consumers that want it */
    char text[RoboInputTextMax];  /* what it produces, UTF-8 */
    uint32_t deadKey;          /* or NONE */
    uint32_t flags;            /* CASED */
} InputLayoutMapping;
```

`InputLayout` holds a bounded table (`RoboInputLayoutMaxMappings`, 128) held **by value** — no allocator, no
lifetime to manage, the same choice the device registry and the keyboard state make. The table is kept in
`(physical, level)` order, because that order is what makes a dump and two runs byte-for-byte equal; a table
whose order depended on insertion would make a test of a dump a test of insertion.

`InputLayoutSetMapping` **replaces** rather than refusing a duplicate: a layout file that lists a key twice means
the second entry, and a door that refused would make the file's order significant in a way no format says. A
*substituted* entry is refused: a layout listing a key whose base character has no identity is a typo, and a
build-time refusal beats a key that silently does nothing.

---

## 3. The two shipped layouts, and the honesty of the second

| Id | What it is |
|---|---|
| `US_ASCII` | 48 positions at two levels: 26 letters, 10 digits, 11 punctuation, space. No dead keys. |
| `US_INTERNATIONAL` | the same, plus five dead accents on the four keys a US-International layout puts them on |

They are **built** (`InputLayoutCreateBuiltin`), from two `char` arrays indexed against one key order, rather
than parsed from a file: this runtime has no layout file format and inventing one now would be a promise about
a feature nobody has asked for. Two parallel `char` arrays rather than string literals, because two strings are
one deleted character away from shifting every mapping after them, and a `sizeof` check against the key order
catches it.

**A layout for another language is data this module can hold and does not ship.** `InputLayoutSetMapping` is
how one arrives. Saying that is better than implying the two tables are the whole world.

---

## 4. Dead keys

A dead key is a key whose meaning is *"the next one, modified"*. That is a property of a layout, and it is why
it lives here rather than in a text engine.

```c
Uchar32 InputDeadKeyCompose(uint32_t deadKey, Uchar32 baseScalar);   /* 0 when there is no form */
Uchar32 InputDeadKeySpacingScalar(uint32_t deadKey);   /* ´ for an acute: what it is on its own */
Uchar32 InputDeadKeyCombiningScalar(uint32_t deadKey); /* U+0301 for an acute: what a mark is */
```

Five accents — acute, grave, circumflex, tilde, diaeresis — each with a spacing form, a combining form, and a
**precomposed-pair table**. The table lists **both cases** of the eight letters these accents attach to rather
than deriving the uppercase form by the Latin-1 offset: that trick holds for every pair except `ÿ`/`Ÿ`, and a
rule with one exception is a rule that is wrong once.

A dead key's mapping **must** carry its spacing form as its text. That is a validity rule, not a convention: a
mapping that armed an accent and produced no character would swallow the key that followed it with nothing to
show for it.

### The four resolutions

| Situation | What comes out |
|---|---|
| dead key then a composable base | **one** precomposed character (`acute` + `e` → `é`) |
| the **same** dead key twice | its **spacing** form (`´`) — that is how a user types an accent on its own |
| a **different** dead key | the old accent's spacing form, and the new accent waits |
| dead key then an incompatible base | the accent's spacing form **and then** the character: `acute` + `q` → `´` then `q` |

That last row is why one chord can produce **two** events, and why `RoboInputTextMaxEvents` is 2 rather than 1 —
the alternative is concatenating into a fragment the event's bound might refuse, or dropping one of the two
characters.

Two other facts are deliberate and asserted:

- a keystroke that means **no character** (a function key, an arrow) does **not** disturb the pending accent —
  pressing `F1` while an accent waits is not a reason to abandon what the user typed;
- a **release** does not abandon it either: the accent waits for a *down*.

`InputTextPipelineCancelDeadKey` is what focus loss, `Escape`, a window deactivation and a device disconnect
call. An accent that outlived the field it was typed into would modify the first character typed into the
*next* field.

---

## 5. What a layout may not do

- **No shaping.** The layout produces characters; RoboText shapes them.
- **No Unicode tables of its own.** The spacing and composing forms are encoded through RoboUnicode's encoder,
  so there is no second UTF-8 implementation in the tree.
- **No scan codes.** A layout maps a `PhysicalKey`, which is a *position*; a backend's `scanCode` is carried on
  the payload and read by nothing, because it is neither portable nor a key id.
- **No `Fn` bit.** The brief names `Fn` "where hardware exposes it"; this runtime's modifier vocabulary
  (`RoboWindowKey.h`) has no such bit, so none is defined here. Inventing one would be a flag that is always
  clear.

---

## 6. Tests

`Tests/Input/SuiteRoboInputLayout.c` — `input.layout.*`, 7 cases, via `TestInput.ps1 -Target Host`:

| Case | What it pins |
|---|---|
| `vocabulary` | the three ids, four levels and six dead keys positionally with their names; the compose table over one row per accent **and** every negative that matters (`tilde + e`, `grave + y`, `acute + q`) |
| `levels` | the AltGr spellings, the bare-Alt rule, caps lock on cased *and* uncased keys, and the control-chord rule |
| `mapping` | one mapping's validity and its refusals, including an undefined flag bit and a dead key with no spacing form |
| `builtin` | both tables' size, order and lookups: `a`/`A`, `1`/`!`, space, an unmapped key as `NotFound`, AltGr as `NotFound`, and the international table's five accents |
| `capslock` | the rule through the **table**, which is where a cased flag that was never set would show up |
| `table` | add, replace in place, and a full table refusing a new key rather than dropping it |
| `dump` | the summary line, the ordering, byte-for-byte determinism, and `?` for a buffer too small |
