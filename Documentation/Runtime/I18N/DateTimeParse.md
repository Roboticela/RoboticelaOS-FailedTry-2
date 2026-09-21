# Date/Time Parsing (Goal 4.23)

Formatting (Goal 4.6) and parsing are **separate APIs**.

```text
localized text
      ↓
RoboI18N date/time parser
      ↓
structured fields (calendar / wall time / offset / zone)
      ↓
calendar conversion (optional)
      ↓
timezone resolution → instant (only when requested)
```

| Concept | Role |
|---|---|
| Formatting | fields / instant → localized UTF-8 |
| Parsing | localized UTF-8 → structured fields |
| Calendar conversion | calendar fields ↔ Gregorian |
| Timezone resolution | local date-time ↔ instant |
| Instant conversion | absolute timeline point |

## Modes

| Mode | Behavior |
|---|---|
| **STRICT** | Exact pattern literals, field ranges, weekday consistency; `h` without day period fails; disambiguation REJECT by default. |
| **PERMISSIVE** | Flexible whitespace literals; numeric width may vary; weekday mismatch ignored; still deterministic. |

## Missing timezone

If the input has no offset/zone:

* Result is a **local** date-time (`have_instant = 0`)
* Never invents UTC or the host machine zone
* Instant conversion requires `options.resolve_instant` **and** either an explicit zone/offset in the input or a resolve timezone on the parser

## Disambiguation

| Policy | Ambiguous fold | Nonexistent gap |
|---|---|---|
| REJECT | fail | fail |
| EARLIER | earlier instant | fail |
| LATER | later instant | fail |
| COMPATIBLE | earlier (Temporal-compatible) | fail |

Gap-fill is not invented; nonexistent local times always fail (timezone engine).

## Patterns & styles

Reuse CLDR patterns via the datetime formatter token compiler:

* style: short / medium / long / full
* explicit pattern
* skeleton → availableFormats

Pattern hour letters are preserved for explicit patterns (locale `hc` remapping does not rewrite intentional `H`/`h`).

Two-digit years use deterministic century windows (Gregorian 1970–2069; Buddhist 2500–2599).

## Zone tokens

| Token | Accepted forms |
|---|---|
| `Z` / `X` / `x` | `Z`, `±HH`, `±HHMM`, `±HH:MM` |
| `O` / `z` / `v` | `GMT`/`UTC` ± offset, or unambiguous IANA id |
| `V` | IANA id (preferred) |

Localized display names are **not** reverse-mapped when ambiguous.

## API

`RoboI18NDateTimeParse.h` — `robo_i18n_datetime_parser_*`, `I18nParseDate` /
`parse_time` / `parse_local_datetime` / `parse_zoned_datetime`.

Input limit: `RoboI18nDtparseInputMax` (4096 bytes).
Error positions: UTF-8 byte offsets.

## Ownership / thread safety

Parsers are stack value types. Not synchronized. Pattern tokens cached inside the parser (no global mutable cache).
