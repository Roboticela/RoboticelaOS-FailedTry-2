# RoboFont Tests

## Suites

| Suite | Goal | Filter prefix |
|---|---|---|
| `SuiteRoboFont.c` | 5.1 | `font.` (core) |
| `SuiteRoboFontSfnt.c` … `SuiteRoboFontValidate.c` | 5.2–5.20 | `font.<area>` |
| `SuiteRoboFontConformance.c` | 5.21 | `font.conform` |
| `SuiteRoboFontSystemPack.c` | 12.2 | `font.systempack` |

## Corpus

See [`fonts/README.md`](fonts/README.md) for category layout, provenance, and
licensing. Prefer synthetic builders; keep on-disk blobs tiny and documented.

## How to run

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "font."
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "font.conform"
.\runtime\Tools\Build.ps1 -Configuration Debug -Sanitize -Verify -Test -Filter "font."
```

## Adding a regression

1. Reproduce with the smallest synthetic SFNT possible.
2. If the failure is a fixed byte pattern, add it under `fonts/malformed/` or
   `fonts/security/` with a README row, then embed the same bytes in the suite.
3. Prefer a `font.conform.*` case for cross-cutting regressions; keep
   goal-specific suites for deep unit coverage.
4. Golden strings must be deterministic (no pointers / timestamps).

## Conformance docs

See [`../docs/Conformance.md`](../docs/Conformance.md).
