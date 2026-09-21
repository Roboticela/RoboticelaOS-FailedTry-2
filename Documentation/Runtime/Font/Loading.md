# Font Loading & Caching (Goal 5.13)

Turns registered discovery faces into fully initialized, cached
`RoboFontFace` objects. Does **not** select, fall back, shape, or rasterize.

```text
discover / register (5.12)
        ↓
load request (registration_id)
        ↓
cache lookup
        ↓
ensure parse (SFNT / TT / name / class / optional cmap+metrics)
        ↓
cache insert (LRU, bounded)
        ↓
select (5.11)
```

## Components

| Type | Role |
|---|---|
| `RoboFontLoader` | Bound to a registry; owns a cache; invalidate hook |
| `RoboFontLoadRequest` | registration_id + load / cache / validate policies |
| `RoboFontLoadResult` | state, face (retained), from_cache, retryable, error |
| `RoboFontCache` | Bounded map keyed by registration_id |
| `RoboFontCacheKey` | registration_id + source_id + face_index (not a pointer) |

## Load states

| State | Meaning |
|---|---|
| `UNLOADED` | Not in cache |
| `LOADING` | In-flight (not published) |
| `LOADED` | Ready; face retained by cache |
| `FAILED` | Permanent failure cached (non-retryable) |
| `EVICTED` | Conceptual; entry removed from cache |

Consumers never observe a partially initialized face.

## Policies

**Load**

| Policy | Parses |
|---|---|
| `MINIMAL` | SFNT + TT + metadata + classification |
| `STANDARD` (default) | + cmap (if present) + metrics |
| `READY` | + outline cache object (glyphs still on demand) |

**Validate**

| Policy | Behavior |
|---|---|
| `NORMAL` | BASIC staged validation; checksum warnings |
| `STRICT` | + TT head checksum verify; table checksum warnings |
| `TRUSTED` | Skip optional probes (parser bounds still on) |
| `PARANOID` | STRICT depth + color/variation/CFF probes; checksum reject |

Bounds checking is never disabled. See `docs/Validate.md` (Goal 5.20).


**Cache**

| Policy | Behavior |
|---|---|
| `DEFAULT` | Insert / hit |
| `BYPASS` | Do not insert; cache hits still allowed |
| `PIN` | Prefer residency during LRU eviction |

## Lazy loading

Discovery (5.12) stores identity + metadata snapshot and a face shell.
`STANDARD` load fills metrics (and cmap when present). Glyph outlines remain
lazy via `FontFaceGlyphOutline`. Unicode coverage (Goal 5.14) is a
separate lazy attach via `FontFaceParseCoverage` and is included in
`approx_face_bytes` when present.

## Cache ownership

- Cache retains the face while the entry exists.
- `load` retains again for the caller (`FontFaceRelease` / `load_result_clear`).
- Eviction releases only the cache retain. Active consumers keep their retain —
  eviction cannot invalidate a held face.

## Limits & eviction

Defaults: 64 faces, ~32 MiB approximate budget (`source_size + overhead`).

LRU among non-pinned, non-in-flight entries. If nothing is evictable and limits
are exceeded → `RoboOutOfMemory`.

## Failures & retry

| Kind | Cached? | `retryable` |
|---|---|---|
| Malformed / unsupported / parse | Yes (FAILED) | 0 |
| I/O, permission, OOM, not found, busy | No | 1 |

## Invalidation

Registry remove / clear / source-remove fires the loader hook:

```text
refresh / remove
      ↓
registry invalidate hook
      ↓
cache drop by registration or source
```

Destroy loader before destroying its registry (hook is cleared on destroy).

## Concurrency

Same model as the registry: single-owner mutations, or external locking.

In-flight entries use `LOADING` + `RoboBusy` so a second overlapping load does
not publish a partial face. Multi-thread: serialize loader calls externally.

## Collections

TTC/OTC faces share the parent `RoboFontSource`. Each registration loads by its
own `face_index` / `registration_id`.

## Example

```c
FontRegistry *reg;
FontLoader *loader;
FontLoadResult lr;
FontDiscoverySourceId sid;
const FontRegistration *rec;

FontRegistryCreate(&reg);
FontLoaderCreate(reg, NULL, &loader);
FontRegistryAddMemorySource(reg, "mem", bytes, n, &sid);
FontRegistryScanSource(reg, sid, 0, NULL);
FontRegistryRegistrationAt(reg, 0, &rec);

FontLoaderLoadRegistration(loader, rec->id, &lr);
/* lr.face → select … */
FontLoadResultClear(&lr);
FontLoaderDestroy(loader);
FontRegistryDestroy(reg);
```

## Fuzz / bench

- `FontLoaderFuzzParse` — bounded registry+loader path
- Tests include repeated cache hits as a lightweight cold/hot baseline

## Out of scope

CSS/web font loading, HTTP fetch, variable fonts, shaping, rasterization,
OS font watching (later goals). Font fallback decisions (Goal 5.15) use a
separate bounded cache keyed by registry generation; they do not share the
loader face-cache invalidate hook.
