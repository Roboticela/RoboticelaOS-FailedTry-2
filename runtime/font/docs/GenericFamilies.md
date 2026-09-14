# Generic Font Families (Goal 5.16)

Maps semantic font categories to configured concrete family names.

> Generic-family resolution does **not** perform text shaping, emoji shaping,
> math layout, or rendering. It does not guess families from installed fonts.

```text
Specific Font Family
        ↓
Generic Font Family
        ↓
Configured Font Candidates
        ↓
Font Selection (5.11)
        ↓
Font Fallback (5.15)
```

## Supported categories

| Identifier | Name | Meaning |
|---|---|---|
| `SERIF` | `serif` | Serif typography |
| `SANS_SERIF` | `sans-serif` | Sans-serif typography |
| `MONOSPACE` | `monospace` | Fixed-pitch typography |
| `CURSIVE` | `cursive` | Handwriting / script-like |
| `FANTASY` | `fantasy` | Decorative / display |
| `SYSTEM_UI` | `system-ui` | Platform-native UI face (explicit map; not assumed equal to sans-serif) |
| `EMOJI` | `emoji` | Fonts intended for emoji (see Goal 5.17 / `docs/Emoji.md`) |
| `MATH` | `math` | Mathematical typography (MATH layout later) |
| `UI_SERIF` | `ui-serif` | Serif UI typography |
| `UI_SANS_SERIF` | `ui-sans-serif` | Sans-serif UI typography |
| `UI_MONOSPACE` | `ui-monospace` | Monospace UI typography |
| `UI_ROUNDED` | `ui-rounded` | Rounded UI typography |
| `NONE` / `UNKNOWN` | `none` | Invalid / unset |

Enums live in `RoboFontStyle.h`. Name interchange: `robo_font_generic_family_name` /
`robo_font_generic_family_from_name`.

## Configuration

`RoboFontGenericConfig` holds explicit ordered lists of concrete family names
per generic (and optional language-scoped overrides).

- Configuration is authoritative — no automatic classification of registry fonts.
- Empty mapping → explicit unresolved (`resolved=0`); never invents a font.
- Duplicate names keep configured order (deterministic).

Portable core ships **empty** platform defaults via
`robo_font_generic_config_create_empty_platform`. Platform ports may supply a
non-empty config later without changing the portable resolver.

## Precedence

First non-empty mapping wins:

1. **Application** config
2. **Runtime** config
3. **Platform** default config
4. Optional **related** generic (`ALLOW_RELATED`):  
   `ui-serif`→`serif`, `ui-sans-serif`/`ui-rounded`→`sans-serif`,  
   `ui-monospace`→`monospace`  
   (`system-ui`, `emoji`, `math` never auto-relate)

Layer replacement (`set_application` / `set_runtime` / `set_platform`) is
atomic: retain new snapshot, release old, bump resolver generation, clear cache.

## Resolution

`robo_font_generic_resolve` → ordered concrete family names.

`robo_font_generic_resolve_select` walks that list and calls Goal 5.11
`robo_font_select` per family (skipping missing families when
`SKIP_MISSING`). First eligible face wins.

`robo_font_generic_resolve_fallback` selects a primary, then reuses Goal 5.15
with the resolved list as `preferred_families`.

## Language / script foundation

Optional BCP-47 `language` on the request selects language-scoped mappings when
configured. Script is carried for future use; this goal does not build a
locale→font database.

## Caching

Bounded decision cache keyed by resolver generation, combined config
generation, registry generation, generic, language, and flags. Config layer
replacement invalidates. Hard capacity cap; not a global unbounded cache.

## Explicitly not in this goal

Emoji presentation policy, CSS parsing, OS font-manager discovery,
language hard-coded font DBs, shaping, OpenType MATH layout, rasterization.

## Chromium

Reference-only. No Chromium headers, libraries, or generic-family maps.
