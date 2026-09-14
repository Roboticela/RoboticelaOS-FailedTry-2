# Font Fallback (Goal 5.15)

Chooses a registered face that can represent requested Unicode content when the
primary face cannot.

> Font fallback chooses a font capable of representing requested content. It
> does **not** perform text shaping, glyph substitution, glyph positioning, or
> rendering.

```text
Requested Font
      ↓
Font Selection (5.11)
      ↓
Coverage Check (5.14)
      ↓
Primary Font
      │
      ├── supports character → use primary
      │
      └── missing character
               ↓
          Fallback Resolver
               ↓
          Candidate Fonts (Registry 5.12)
               ↓
          Coverage + Classification
               ↓
          Best Fallback Font
```

## Layering

```text
Discovery → Registry → Selection → Coverage → Fallback → (Phase 6) Shaping
```

Fallback does not discover fonts, scan directories, or load from the OS.
Candidates come only from the current `RoboFontRegistry`.

## Components

| Type | Role |
|---|---|
| `RoboFontFallbackResolver` | Bound to a registry; owns policy copy + decision cache |
| `RoboFontFallbackRequest` | Primary face, code points, style, language/script hints |
| `RoboFontFallbackResult` | Selected face (retained), stage, score, cache flag |
| `RoboFontFallbackPolicy` | Preferred families, language preferences, last-resort, limits |
| `RoboFontFallbackScore` | Deterministic ranking after coverage gate |

## Coverage-first behavior

For every requested scalar, usable coverage (`RoboFontCoverage` /
`MAPPED_USABLE`) is required before a face can win.

- Primary covers all → **no registry scan** (fast path).
- `.notdef` (GID 0) and unsupported mappings do **not** count as coverage.
- A stylish but uncovered candidate never beats a covered one.

## Candidate filtering

Before scoring, candidates must:

- Be currently registered (valid face pointer / id)
- Differ from the primary face id
- Provide usable coverage for the request (hard gate)

Family mismatch alone does **not** eliminate a candidate — fallback often
uses another family.

## Scoring (after coverage)

Lower is better. Compare with `robo_font_fallback_score_compare`.

1. **stage_rank** — PRIMARY &lt; PREFERRED &lt; SCRIPT &lt; GENERAL &lt; LAST_RESORT
2. **preferred_family_rank** — request list, then policy list (index order)
3. **language_rank** — soft; only when `language` is set **and**
   `policy.language_preferred_families` matches (no hard-coded language DB)
4. **script_rank** — 0 if coverage reports the requested script; else 1
5. **stretch / style / weight** — Goal 5.11 selection distances (not a duplicate matcher)
6. **face_id** — stable tie-break

WWS is recorded on the score for diagnostics; distances already use
classification angles where available.

## Fallback chain / stages

| Stage | Meaning |
|---|---|
| `PRIMARY` | Primary already covers the request |
| `PREFERRED` | Family listed in request or policy preferred lists |
| `SCRIPT` | Coverage has the requested/derived script |
| `GENERAL` | Any other covering registered face |
| `LAST_RESORT` | Configured last-resort family or face id |
| `NONE` | No covering face; `found=0` (explicit no-fallback) |

The chain is configurable via policy. There is no universal hard-coded font.

## Language & script

- **Script** is derived from the first scalar (Unicode Script / SCX) unless
  `has_script` is set. Script relevance is a soft ranking signal, not language.
- **Language** (optional BCP-47) influences ranking only through caller-supplied
  `language_preferred_families`. No language→font database in this goal.

## Style preservation

Requests carry weight / stretch / style (and optional oblique angle). Fallback
reuses Goal 5.11 distance helpers so an italic/600/normal covering face is
preferred over an unrelated regular when both cover.

## Sequence foundation

`codepoints[]` supports single scalars and short sequences (base+mark, future
emoji / Indic / Arabic clusters) **without shaping**.

Default `REQUIRE_ALL`: every scalar must be usable. A font that has a combining
mark but not its base does not win for a base+mark sequence.

Emoji scalars may appear in requests; emoji font selection policy, COLR, ZWJ
shaping, and presentation logic are **not** implemented here (later goals).

## Last resort & no-fallback

If no registered face covers the request:

- `found = 0`, `stage = NONE`
- Optionally a configured last-resort face may still win **only if it covers**
- Callers decide missing-glyph presentation; fallback never fabricates glyphs

## Cache

Bounded decision cache (default 256, hard cap 4096). Keys include:

- registry generation
- primary face id
- code points / style / language / script / flags / policy hash

Hits that cannot re-resolve a face id are discarded and recomputed.
Registry add/remove/refresh bumps generation → stale entries miss.
Use `ROBO_FONT_FALLBACK_FLAG_NO_CACHE` to bypass.

## Concurrency & errors

Concurrent resolve is safe for shared results after retain. Faces returned in
results are retained; `robo_font_fallback_result_clear` releases them.

Failures return `RoboError` codes (invalid scalars, missing primary, OOM).
Broken candidates are skipped; resolve does not crash on unresolvable fallback.

## API sketch

| API | Role |
|---|---|
| `robo_font_fallback_resolver_create` / `_destroy` | Lifetime |
| `robo_font_fallback_resolver_set_policy` | Replace owned policy copy |
| `robo_font_fallback_resolve` | Full request |
| `robo_font_fallback_resolve_codepoint` | Single-scalar helper |
| `robo_font_fallback_face_covers` | Coverage probe |
| `robo_font_fallback_resolver_cache_clear` / `_stats` | Cache control |
| `robo_font_fallback_fuzz_resolve` | Malformed / pathological entry |

## Explicitly not in this goal

Generic families, emoji selection policy, CSS `font-family` lists, system
fallback tables, shaping, GSUB/GPOS, ligatures, kerning, mark attachment,
variable instances, rasterization, color glyphs.

## Chromium

Chromium is reference-only. No Chromium headers, libraries, namespaces, or
fallback lists are used.
