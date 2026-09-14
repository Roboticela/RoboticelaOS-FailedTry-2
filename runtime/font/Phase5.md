# Phase 5 — RoboFont (complete)

| Item | Value |
|---|---|
| Goals | **5.1–5.22** |
| API | **0.22.0** (`ROBO_FONT_API_FROZEN=1`) |
| Brand | **RoboFont API v1** |
| Freeze doc | [`ApiFreeze.md`](ApiFreeze.md) |

## Goal map

| Goal | Topic |
|---|---|
| 5.1 | Architecture & font model |
| 5.2 | SFNT container |
| 5.3 | TrueType tables |
| 5.4 | OpenType structures |
| 5.5 | CFF/CFF2 |
| 5.6 | cmap |
| 5.7 | Metrics |
| 5.8 | Glyph outlines |
| 5.9 | Naming / metadata |
| 5.10 | Style / weight / stretch |
| 5.11 | Selection |
| 5.12 | Discovery / registry |
| 5.13 | Loading / cache |
| 5.14 | Coverage |
| 5.15 | Fallback |
| 5.16 | Generic families |
| 5.17 | Emoji fonts |
| 5.18 | Variation fonts |
| 5.19 | Color fonts |
| 5.20 | Validation / security |
| 5.21 | Conformance tests |
| 5.22 | Final integration / API freeze |

## Verification

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "font."
.\runtime\Tools\Build.ps1 -Configuration Release -Verify -Test -Filter "font."
.\runtime\Tools\Build.ps1 -Configuration Debug -Sanitize -Verify -Test -Filter "font."
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "font.p5"
```

`Verify` asserts guest ELF integrity and **no Chromium paths** in the RoboFont build.

## Next phase

Phase 6 — RoboText & text shaping — consumes RoboFont API v1. Do not reopen
Phase 5 APIs without a new milestone.
