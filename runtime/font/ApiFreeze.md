# ApiFreeze (Goal 5.22)

**Status:** Phase 5 public API is **frozen** at RoboFont **0.22.0**
(`ROBO_FONT_API_FROZEN == 1`).

**Branding:** **RoboFont API v1** refers to this frozen Phase 5 surface.

## Frozen public surface

Applications and later phases (RoboText, RoboCSS, RoboLayout, RoboGraphics, …)
may depend on:

* `RoboFont/RoboFont.h` (umbrella)
* All `RoboFont/RoboFont*.h` headers under `runtime/Include/RoboFont/`
* `RoboFont/RoboFontVersion.h` pin macros (`ROBO_FONT_API_FROZEN`, version string)

## API classification

| Class | Meaning | Examples |
|---|---|---|
| **public** | Stable ABI for apps / later phases | Face, Instance, Source, Registry, Loader, Selection, Fallback, Metrics, Outline, Validate, … |
| **public harness** | Stable for fuzz/conformance harnesses; not for app logic | `*_fuzz_parse`, `robo_font_conform_*`, golden helpers |
| **internal** | May change without notice | `Source/RoboFont/*Internal.h`, private `.c` helpers |
| **test-only** | Suite fixtures / corpus under `font/tests/` | Synthetic SFNT builders in suites |

## Not public

Do **not** treat as application API:

* Anything under `runtime/Source/RoboFont/` that is not re-exported by Include/
* Chromium / Skia / FreeType / HarfBuzz / OTS types or headers (forbidden)
* OS font-manager types
* Suite-local synthetic builders

## Stability rules

| Concern | Contract |
|---|---|
| Naming | `robo_font_*` / `ROBO_FONT_*` |
| Errors | `robo_result_t` (+ validation issue codes for security diagnostics) |
| Ownership | Retain/release for heap objects; see Architecture.md / this freeze |
| Lifetime | Faces remain valid while retained; cache eviction must not invalidate active retains |
| Thread safety | Immutable parsed snapshots are shareable; registry/loader mutations are single-owner unless documented |
| Enums | Numeric values stable within 0.22.x; new members append only |
| Versioning | Breaking changes require bumping `ROBO_FONT_VERSION` and clearing freeze only via a new milestone |

## Ownership (summary)

| Object | Ownership |
|---|---|
| `robo_font_source_t` | Retain/release; face retains source |
| `robo_font_face_t` | Retain/release; registry borrows while registration lives |
| `robo_font_instance_t` | Retain/release; does not mutate parent face |
| Parsed backends (TT/cmap/…) | Owned by face after `parse_*`; immutable thereafter |
| Glyph outline / metrics snapshots | Caller-owned results; release where API provides |
| Registry | Owns registrations; destroy invalidates borrowed registration pointers |

## Thread safety (summary)

| API class | Classification |
|---|---|
| Immutable queries on fully parsed faces | **thread-safe** for concurrent readers |
| `parse_*` on a face | **thread-compatible** — serialize per face |
| Registry add/scan/remove/destroy | **single-thread-only** (external sync for readers during mutation) |
| Loader cache mutations | **single-thread-only** for cache write path |
| Fuzz entry points | **thread-compatible**; no shared mutable state across calls |

## Intentional non-goals (frozen as unsupported)

Text shaping (GSUB/GPOS execution), script shaping, rasterization, CSS font matching,
web-font download policy, compositor integration, OS font-manager enumeration as the
primary discovery path, DRM/licensing enforcement.

## Layer notes (not duplicates)

* `RoboFontOt` cmap = structural encoding records; `RoboFontCmap` = Unicode→GID lookup.
* `RoboFontStyle` = value types; `RoboFontClassification` = face classification.
* Selection and fallback remain separate responsibilities.
