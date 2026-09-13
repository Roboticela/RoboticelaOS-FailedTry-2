# RoboMsg — application message catalogs (Goal 4.11)

Application translations are **not** CLDR data. RoboMsg is the Roboticela
localization resource format for stable message IDs and per-locale templates.

## Pipeline

```text
*.robomsg.json (source, UTF-8)
        ↓
generate_messages.py  (validate + compile)
        ↓
RoboMsgData / binary blob
        ↓
robo_i18n_catalog_* / robo_i18n_message_*
```

`Build.ps1` does **not** download or regenerate catalogs.

## Message identifiers

| Rule | Value |
|---|---|
| Syntax | `[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)*` |
| Case | **sensitive** |
| Max length | 128 bytes |
| Uniqueness | `(id, context)` unique within a catalog |
| Ownership | Application / component owns IDs; runtime never invents them |

Examples: `app.login.title`, `sys.power.shutdown_confirm`.

## Context

Optional `context` string (translator metadata / disambiguation). Not shown to
users. Empty context is the default key. Two entries may share English source
text but must use different `(id, context)` pairs (prefer distinct IDs).

## Source format (`.robomsg.json`)

```json
{
  "format_version": 1,
  "catalog_id": "robo.demo",
  "catalog_version": "1.0.0",
  "locale": "en",
  "messages": [
    {
      "id": "app.greeting",
      "text": "Hello, {name}!",
      "context": "",
      "description": "Greeting with user name"
    },
    {
      "id": "app.items",
      "plural": {
        "one": "{count} item",
        "other": "{count} items"
      }
    }
  ]
}
```

Required: `format_version`, `catalog_id`, `catalog_version`, `locale`, `messages`.
Each message: `id`, and either `text` **or** `plural` (not both).
`plural.other` is required when `plural` is used.

## Placeholders

Syntax: `{name}` where `name` = `[A-Za-z_][A-Za-z0-9_]*`.

| Escape | Result |
|---|---|
| `{{` | literal `{` |
| `}}` | literal `}` |

Rules:

- Named only (no `{0}` positional).
- Compiler requires the same placeholder **set** across locales for a given
  `(id, context)`.
- Runtime `message_format` substitutes UTF-8 values; missing vars → error or
  leave `{name}` per options; no code execution.
- Templates are inert text — never evaluated as expressions.

## Plural architecture

Plural maps use CLDR category names: `zero`, `one`, `two`, `few`, `many`,
`other`. Selection uses `robo_i18n_plural_*` (no duplicated plural logic).
Missing category for a selected result falls back to `other` within the same
locale entry, then catalog locale fallback.

This is **not** ICU MessageFormat (no select/gender nesting in 4.11).

## Locale fallback

Per lookup (explicit `fallback_locale` on options / catalog default):

```text
requested locale
  → exact catalog locale
  → truncation parents (robo_i18n_locale_parent)
  → configured fallback locale (may be empty → skip)
  → missing-message policy
```

**English is not assumed.** Configure `fallback_locale` explicitly (tests often
use `"en"` as the *configured* fallback).

## Missing-message policy

| Mode | Behavior |
|---|---|
| `NOT_FOUND` | `ROBO_NOT_FOUND` |
| `RETURN_ID` | copy message id; `ROBO_OK` |

Never returns an empty string for a successful lookup of a missing translation.

## Binary catalog (runtime)

Magic `RMSG` (LE `0x47534D52`), `format_version == 1`. Incompatible versions are
rejected. Generated demo catalog embeds both C tables and a validated blob.

## Ownership / threads

- Catalogs are **immutable** after open/load.
- Multiple threads may read the same catalog concurrently.
- No process-wide “current language”; pass locale per call.
- Output buffers are caller-owned; string pool pointers are valid for the
  catalog lifetime only when using `message_get` into caller buffers (API copies).

## Security

Treat blobs as untrusted unless the build guarantees them. Loader checks magic,
version, bounds, and UTF-8. Malformed catalogs → error, never crash.
