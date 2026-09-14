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

Default IO is a **stub** (`ROBO_NOT_SUPPORTED`). Tests and app-bundled trees use
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

- Probe face count via `robo_font_sfnt_probe_face_count`
- Create one `RoboFontFace` per index on the shared source bytes
- Distinct `face_id` / `face_index`; `is_collection = 1`
- Bytes are not duplicated per face

## Registration lifecycle

1. `robo_font_registry_create`
2. Optionally `robo_font_registry_set_io`
3. `add_memory_source` / `add_file_source` / `add_directory_source`
4. `scan_source` or `scan_all`
5. Query / `collect_faces` → `robo_font_select`
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
   style → skip (unless `ROBO_FONT_DISCOVERY_SCAN_ALLOW_CONTENT_DUP`).
3. Never silently replace an existing registration with an unrelated font.

Full-file hashing is intentionally avoided during discovery.

## Refresh

`robo_font_registry_refresh_source` drops registrations for that source, then
scans again. No OS-wide filesystem watcher in Goal 5.12.

## Security limits

| Limit | Constant |
|---|---|
| Max path length | `ROBO_FONT_DISCOVERY_MAX_PATH` |
| Max file size | `ROBO_FONT_DISCOVERY_MAX_FILE_SIZE` |
| Max files per scan | `ROBO_FONT_DISCOVERY_MAX_FILES_PER_SCAN` |
| Max faces per collection | `ROBO_FONT_DISCOVERY_MAX_FACES_PER_COLLECTION` |
| Max directory depth | `ROBO_FONT_DISCOVERY_MAX_DIR_DEPTH` |
| Max registrations | `ROBO_FONT_REGISTRY_MAX_REGISTRATIONS` |
| Max sources | `ROBO_FONT_REGISTRY_MAX_SOURCES` |

Malformed or inaccessible entries increment `failed` / skip counters; the scan
continues unless `ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST`.

## Thread safety

Mutations (add / scan / remove / clear / destroy) are **single-owner**.
Concurrent readers are safe only while no mutation runs (caller provides locking).
Face refcounts are not atomic (same as Goal 5.1). After remove/destroy, borrowed
face pointers from registrations must not be used.

## Example

```c
robo_font_registry_t *reg = NULL;
robo_font_discovery_source_id_t sid;
robo_font_discovery_result_t result;
robo_font_face_t **faces = NULL;
uint32_t n = 0;
robo_font_select_request_t req;
robo_font_selection_t sel;

robo_font_registry_create(&reg);
robo_font_registry_add_memory_source(reg, "mem:Probe", bytes, size, &sid);
robo_font_registry_scan_source(reg, sid, 0, &result);
robo_font_registry_collect_faces(reg, &faces, &n);

robo_font_select_request_init(&req);
robo_font_selection_init(&sel);
req.family = "Probe";
robo_font_select(&req, faces, n, &sel);
/* use sel.face … */
robo_font_selection_clear(&sel);
robo_free(faces);
robo_font_registry_destroy(reg);
```

## Fuzz

`robo_font_discovery_fuzz_parse` accepts arbitrary bytes / empty input and must
not crash (bounded virtual tree + registry).

## Out of scope

System font directories, platform font managers, fallback, generic families,
emoji selection, CSS matching, install/uninstall, watchers, variable fonts,
shaping, rasterization (later goals).
