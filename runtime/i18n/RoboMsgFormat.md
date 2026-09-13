# RoboMsgFmt — message formatting engine (Goal 4.12)

RoboMsgFmt is a **Roboticela-specific** UTF-8 message template language.
It is inspired by ICU MessageFormat concepts but is **not** a full ICU
MessageFormat implementation.

Language choice: **C** (consistent with RoboI18N).

## Pipeline

```text
catalog (4.11) → template UTF-8 → mf_parse → AST → mf_format → UTF-8
```

`parse once / format many` is supported via `robo_i18n_mf_parse` +
`robo_i18n_mf_format`.

## Syntax (v1)

```text
pattern     := piece*
piece       := literal | '{' name '}' | '{' name ',' type [',' style] '}' | '#'
literal     := text | '{{' | '}}'
type        := number | date | time | select | plural | selectordinal
```

### Escapes

| Input | Where | Output |
|---|---|---|
| `{{` | any depth | `{` |
| `}}` | **top-level pattern only** | `}` |

Inside nested branch bodies (`select` / `plural` / `selectordinal`), a single
`}` always **closes** that body. This keeps constructs like
`{g, select, other {They}}` unambiguous (body close + argument close).
Literal `}` inside nested bodies is not expressible in v1 (extension point).

`#` is only valid **inside** `plural` / `selectordinal` branch bodies.
It expands to the plural argument formatted with RoboI18N number formatting.

## Versioning (Goal 4.13)

| Concept | Value |
|---|---|
| RoboMsgFmt syntax version | **1** (`ROBO_I18N_MF_SYNTAX_VERSION`) |
| Optional prologue | `#!robo-mf/1` as the first line |

Templates without a prologue are treated as syntax version **1**.
Unsupported `#!robo-mf/N` → `ROBO_NOT_SUPPORTED` (never silently reinterpreted).

RoboMsgFmt is **not** ICU MessageFormat. CLDR/Unicode provide plural and
number/date data; they do not define this template language.

## Validation & compatibility (Goal 4.13)

API: `RoboI18NMessageValidate.h` (C).

| Capability | API |
|---|---|
| Validate (no format) | `robo_i18n_message_validate` / `mf_validate_ex` |
| Structured diagnostics | `robo_i18n_mf_diagnostic_t` (UTF-8 **byte** offset, line/column) |
| Argument contract | `robo_i18n_mf_analyze` / `mf_analyze_template` |
| Contract compatibility | `robo_i18n_mf_contracts_compatible` |
| Catalog scan | `robo_i18n_mf_catalog_validate` |
| Fallback interface check | `robo_i18n_mf_catalog_validate_fallback` |

**Interface compatibility:** shared argument names must have compatible usage
masks (string/select; number/plural/ordinal; date; time). Locale may omit or
add arguments; missing message IDs vs a reference locale are **warnings**.
Wording differences are ignored.

**Duplicate branches** (same select key or same `=N`) are parse errors.

Configurable limits via `robo_i18n_mf_limits_t` (zero field → built-in default).


### Simple / number

```text
Hello, {name}!
You have {count, number} messages.
```

### Select (byte-exact key match)

```text
{gender, select,
    male {He}
    female {She}
    other {They}
}
```

Matching: UTF-8 **byte-exact** against the string form of the argument
(`string` as-is; `bool` → `true`/`false`; `i64` → ASCII decimal).
**No** collation. `other` is **required** at parse time.

### Plural / selectordinal

```text
{count, plural,
    =0 {no messages}
    one {# message}
    other {# messages}
}
```

Exact selectors (`=N`) are tried first when the value is an integral decimal
(no visible fraction digits). Otherwise CLDR cardinal (`plural`) or ordinal
(`selectordinal`) categories from Goal 4.4 are used. `other` is required.

### Date / time

```text
{d, date, short}
{t, time, medium}
```

Styles: `short` | `medium` | `long` | `full` (default `medium`).
Delegates to `RoboI18NDateTimeFormat` (Gregorian / UTC).

## Argument values

| Kind | Notes |
|---|---|
| string | borrowed UTF-8 |
| i64 / u64 / decimal | preferred for plurals / `#` |
| double | best-effort via existing decimal conversion |
| bool | select keys `true`/`false` |
| date / time / datetime / instant | date/time args |

Do not silently route all numbers through `double`.

## Limits

| Limit | Value |
|---|---|
| Template size | 8192 bytes |
| Nesting depth | 16 |
| Branches per select/plural | 32 |
| Arguments | 32 |
| Output | 16384 bytes |

## Errors

Malformed templates → `ROBO_PARSE_ERROR` (no crash).
Missing args / unmatched select → `ROBO_NOT_FOUND`.
Output overflow → `ROBO_ERROR`.

## Not supported (yet)

- Full ICU MessageFormat / nested format styles
- `offset:` on plural
- Currency / percent / compact number styles in messages
- Gender grammar beyond `select`
- Rich text / HTML in messages
- Bidirectional visual reordering (logical UTF-8 only)

## Ownership / threads

- Parsed messages: heap; caller destroys; immutable after parse
- Format: re-entrant; no global mutable cache
- Arg string/date pointers: borrowed for the duration of `format`
