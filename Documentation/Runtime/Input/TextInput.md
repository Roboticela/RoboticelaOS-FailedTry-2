# The text input pipeline

`Runtime/Include/RoboInput/RoboInputText.h`, `Runtime/Source/RoboInput/RoboInputText.c` — Phase 13.11 (with
13.8, the command layer it feeds).

---

## 1. The distinction this file exists for

> A **key event** means *this physical or logical key changed state*.
> A **text event** means *this text should be inserted into whatever is focused*.

They are different questions, and the brief makes the separation mandatory for a concrete reason: a runtime
that inserted a character on `KeyDown` would make `Ctrl+C` both fire the copy command **and** type a `c` into
the document, and would make an arrow key, a function key and a modifier all produce characters.

So there are two event types, and exactly one door turns the first into the second:

```c
Result InputTextPipelineTranslate(InputTextPipeline *pipeline,
                                  const InputEvent *keyEvent,
                                  InputEvent *outEvents,
                                  uint32_t outCapacity,
                                  uint32_t *outCount,
                                  InputTextOrigin *outOrigin,
                                  InputDiag *diag);
```

It is a **pipeline** rather than a line in a keyboard driver because two things sit between a key and a
character that are not the driver's business: the **layout** (which position means which character, 13.9) and
**dead keys** (which character means "wait for the next one", 13.10). What it deliberately does not do is insert
anything. The `TEXT_INPUT` events it produces are handed to whatever owns the focused editor, which is
RoboText's (12.22) — reached through 13.12/13.13's command layer, never from this file.

---

## 2. The order of the questions *is* the pipeline

| Step | Question | If the answer is no |
|---|---|---|
| 1 | is this a key event, and is it an **up**? | a non-key event is **refused**; an up produces nothing and is *accepted* |
| 2 | does the layout map this key **at all**? (asked at the base level) | nothing, counted as an empty chord |
| 3 | is the chord a **control** chord? | nothing — the chord is a command |
| 4 | which level, and does the layout have a mapping there? | nothing (AltGr on a US table) |
| 5 | is an accent pending? | compose / repeat / replace / reject (§3) |
| 6 | does the mapping produce text, or *arm* an accent? | arm, and produce nothing |

Every "no" is an honest empty answer rather than an error, which is what lets a caller hand **every** key event
to this one door and let the ones that mean nothing fall through. Only three things are refusals:
a non-key event, a NULL argument, and an output array shorter than `RoboInputTextMaxEvents` — and that last one
is a refusal rather than a truncation because one chord may legitimately produce two events and a caller with
room for one would silently lose half of what the user typed.

A refusal **changes nothing**, including not consuming a pending accent.

---

## 3. Outcomes, and the origin report

`InputTextOrigin` says *why* a chord did what it did. It is a **report** — nothing above branches on it — read
by a diagnostics page and asserted by the suite:

| Origin | Meaning |
|---|---|
| `NONE` | nothing produced, nothing pending (an unmapped key, a control chord, an up) |
| `KEY` | the layout's character |
| `COMPOSED` | a dead key and a following key produced one precomposed character |
| `DEAD_KEY_REPEATED` | the same accent twice: its spacing form |
| `DEAD_KEY_REPLACED` | a different accent: the old spacing form, the new accent waits |
| `DEAD_KEY_REJECTED` | no precomposed form: the spacing form **and** the character |
| `DEAD_KEY_PENDING` | an accent is waiting; no text, and none lost |

**There is one origin per call, and it is the chord's** — not one per event. Both events of a rejected accent
share it, and every other origin is chord-level by construction, so a door that wrote one per event would make
every caller pass an array to answer a question that has one answer; a caller who passed a scalar would have
the second write land on whatever the compiler put beside it. (That is not hypothetical: it is how this was
found, with the suite's own `InputTextOrigin` on the stack next to the event array.)

---

## 4. What the produced events carry

A `TEXT_INPUT` event inherits the key event's **device, timestamp, modifiers, window and display**, and carries
the produced text. It names **no plane** — a text event has no position, which is the model's own rule and is
asserted through the model's validator rather than by reading a field. It is never targeted: where the text goes
is the focused editor's business, and this pipeline does not know what is focused.

A chord's two events share one timestamp, because one keystroke produced both.

---

## 5. Capabilities, and what is still absent

13.9/13.10/13.11 set `keyboardLayout`, `deadKeys` and `textInput` in `InputCapabilities`, plus
`layoutCount`, `layoutLevelCount`, `deadKeyCount` and `textEventsPerChord`, and took the module to **`0.3.0`**.

Three flags deliberately stay **0**, and each is a different absence:

- **`textEditing`** — the text reaches no editor yet. 13.12/13.13's command layer (`TextEditCommand`, the
  shortcut manager) is what will carry it into RoboText's editor, and until then this module produces text that
  nothing consumes.
- **`textComposition`** — a dead key is **not** a composition. A composition is an input *method*'s preedit:
  `COMPOSITION_START`/`UPDATE`/`COMMIT`/`CANCEL`, clause ranges, a candidate window positioned from RoboText
  caret geometry. That is 13.37–13.39.
- **`deviceReading`** and **`platformAdapters`** — nothing opens a keyboard; the layout and the pipeline are
  fed by whatever a caller (a test, a recording, a future adapter) hands them.

---

## 6. Tests

`Tests/Input/SuiteRoboInputText.c` — `input.text.*`, 11 cases, via `TestInput.ps1 -Target Host`:

| Case | What it pins |
|---|---|
| `vocabulary` | the seven origins positionally, and that a chord's maximum is two |
| `init` | a fresh pipeline: no layout, no accent, zero counters; a NULL layout is *a* layout (every key produces nothing); the refusals |
| `kinds` | a pointer event refused, the argument refusals, and a key **up** accepted as "no character" rather than refused |
| `plain` | one key, one event, with device/timestamp/modifiers/window inherited, no plane, and an unmapped key as an empty chord |
| `levels` | shift, caps lock, caps lock with shift, AltGr on a table with no third level, and a control chord producing nothing |
| `deadkey` | arming (no event), a keystroke that means nothing not disturbing it, a release not abandoning it, composition, and the shifted accents |
| `repeat` | the spacing form, for two different accents |
| `replace` | the old spacing form emitted, the new accent waiting, and it composing afterwards |
| `reject` | **two** events, in the order they were typed, sharing one timestamp |
| `cancel` | cancellation drops the accent and changes nothing else; cancelling nothing is reported |
| `dump` | one line, counted by hand, byte-for-byte over two calls, `?` when it does not fit, and `dead=acute` when an accent waits |
