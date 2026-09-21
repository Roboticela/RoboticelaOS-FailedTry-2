# Font Discovery (Goal 5.12)

Locate fonts from **explicit** sources, parse identity metadata with existing
RoboFont parsers, and register faces for later selection. Discovery does **not**
select, fall back, shape, or render.

```text
source
  ↓
discover (filter → classify → probe)
  ↓
register (metadata + classification; lazy cmap/outlines)
  ↓
query (registry)
  ↓
select (Goal 5.11 — caller-supplied faces)
```

## Architecture

| Concept | Role |
|---|---|
| `RoboFontDiscovery` | Scan sources; filter candidates; ingest bytes |
| `RoboFontDiscoverySource` | Memory blob, single file, or directory |
| `RoboFontDiscoveryResult` | Counters: registered / skipped / failed |
| `RoboFontRegistry` | Owns registrations and discovery sources |
| `RoboFontRegistration` | Stable entry: IDs, origin, family snapshot, face |

Does **not** duplicate `RoboFontFace` or `RoboFontSource`. Faces and sources are
created through existing APIs; the registry retains faces until remove/destroy.

## Supported sources

| Kind | Behavior |
|---|---|
| Memory | Caller supplies bytes; copied into a `RoboFontSource` |
| File | Path read through injectable IO on scan |
| Directory | Enumerate entries; optional recursive (depth-limited) |

Core library does **not** scan OS system font directories. Platform code may later
supply paths and a real filesystem IO implementation.

Default IO is a **stub** (`RoboNotSupported`). Tests and app-bundled trees use
**virtual in-memory IO** (`robo_font_discovery_virtual_*`).

## Formats

| Extension / signature | Result |
|---|---|
| `.ttf` / scaler `00010000` / `'true'` / `'typ1'` | TrueType candidate |
| `.otf` / `'OTTO'` | OpenType/CFF candidate |
| `.ttc` / `.otc` / `'ttcf'` | Collection — one registration per face |
| Other | Filtered or unsupported (not parsed as fonts) |

Cheap filters run first: extension, minimum size, SFNT signature. Only candidates
reach full face create + metadata/classification.

## Collections (TTC/OTC)

- Probe face count via `FontSfntProbeFaceCount`
- Create one `RoboFontFace` per index on the shared source bytes
- Distinct `face_id` / `face_index`; `is_collection = 1`
- Bytes are not duplicated per face

## Registration lifecycle

1. `FontRegistryCreate`
2. Optionally `FontRegistrySetIo`
3. `add_memory_source` / `add_file_source` / `add_directory_source`
4. `scan_source` or `scan_all`
5. Query / `collect_faces` → `FontSelect`
6. `remove_registration` / `refresh_source` / `destroy`

Lazy loading: discovery parses metadata + classification for registration.
Cmap, metrics detail, and glyph outlines stay on-demand via face APIs.

## Identity

| ID | Stability |
|---|---|
| `registration_id` | Stable for the lifetime of that registry entry |
| `face_id` | Stable while the face object lives (session-local) |
| `origin` + `face_index` | Unique within one discovery `source_id` |

Neither registration nor face IDs are persistent across processes or machines.
Do not use pointer values as identity.

## Duplicate policy

1. **Same origin key** — `(source_id, origin, face_index)` already registered →
   skip (`skipped_duplicate`). Origin is the memory label or file path so a
   directory source can hold many files that all use `face_index` 0.
2. **Content heuristic** — non-empty PostScript name + matching weight / stretch /
   style → skip (unless `RoboFontDiscoveryScanAllowContentDup`).
3. Never silently replace an existing registration with an unrelated font.

Full-file hashing is intentionally avoided during discovery.

## Refresh

`FontRegistryRefreshSource` drops registrations for that source, then
scans again. No OS-wide filesystem watcher in Goal 5.12.

## Security limits

| Limit | Constant |
|---|---|
| Max path length | `RoboFontDiscoveryMaxPath` |
| Max file size | `RoboFontDiscoveryMaxFileSize` |
| Max files per scan | `RoboFontDiscoveryMaxFilesPerScan` |
| Max faces per collection | `RoboFontDiscoveryMaxFacesPerCollection` |
| Max directory depth | `RoboFontDiscoveryMaxDirDepth` |
| Max registrations | `RoboFontRegistryMaxRegistrations` |
| Max sources | `RoboFontRegistryMaxSources` |

Malformed or inaccessible entries increment `failed` / skip counters; the scan
continues unless `RoboFontDiscoveryScanFailFast`.

## Thread safety

Mutations (add / scan / remove / clear / destroy) are **single-owner**.
Concurrent readers are safe only while no mutation runs (caller provides locking).
Face refcounts are not atomic (same as Goal 5.1). After remove/destroy, borrowed
face pointers from registrations must not be used.

## Example

```c
FontRegistry *reg = NULL;
FontDiscoverySourceId sid;
FontDiscoveryResult result;
FontFace **faces = NULL;
uint32_t n = 0;
FontSelectRequest req;
FontSelection sel;

FontRegistryCreate(&reg);
FontRegistryAddMemorySource(reg, "mem:Probe", bytes, size, &sid);
FontRegistryScanSource(reg, sid, 0, &result);
FontRegistryCollectFaces(reg, &faces, &n);

FontSelectRequestInit(&req);
FontSelectionInit(&sel);
req.family = "Probe";
FontSelect(&req, faces, n, &sel);
/* use sel.face … */
FontSelectionClear(&sel);
RoboFree(faces);
FontRegistryDestroy(reg);
```

## Fuzz

`FontDiscoveryFuzzParse` accepts arbitrary bytes / empty input and must
not crash (bounded virtual tree + registry).

## Out of scope

System font directories, platform font managers, fallback, generic families,
emoji selection, CSS matching, install/uninstall, watchers, variable fonts,
shaping, rasterization (later goals).
