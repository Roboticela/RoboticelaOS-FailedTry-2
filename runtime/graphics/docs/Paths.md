# Paths (Goal 6.8)

Canonical path **data model** for RoboGraphics. Geometry only — no fill,
stroke, flatten, or rasterization.

```text
MoveTo / LineTo / Close
        ↓
owned command sequence (RoboMem)
        ↓
read-only iteration + bounds
        ↓
(later) transform → flatten → fill/stroke → paint
```

## Purpose

Paths represent vector contours that later systems (transforms, clipping,
painting, layout) will consume. This goal freezes the command model and
ownership so those consumers share one representation.

## Supported commands

| Verb | Payload | Effect |
|---|---|---|
| `ROBO_GFX_PATH_MOVE_TO` | point | Begin a new subpath; set current + contour start |
| `ROBO_GFX_PATH_LINE_TO` | point | Append a straight segment (requires open contour) |
| `ROBO_GFX_PATH_CLOSE` | none | Close the open contour; current → contour start |

Reserved (not constructible in 6.8):

| Verb | Future use |
|---|---|
| `ROBO_GFX_PATH_QUAD_TO` | Quadratic Bézier |
| `ROBO_GFX_PATH_CUBIC_TO` | Cubic Bézier |

Verb values are stable so curve commands can be added without redesigning
the path API or iteration surface.

## Subpath semantics

| Concept | Meaning |
|---|---|
| Current point | Last MoveTo/LineTo destination; after Close, contour start |
| Contour start | Point of the MoveTo that opened the subpath |
| Open contour | Accepts LineTo / Close |
| Closed contour | After Close; LineTo is invalid until the next MoveTo |

Rules:

- `MoveTo` always starts a new subpath. It does **not** insert an implicit
  Close for a previous open contour.
- `LineTo` without an open contour → `ROBO_INVALID_ARGUMENT` (path unchanged).
- `Close` with no open contour → `ROBO_OK` no-op (no verb appended).
- `Close` after MoveTo alone is valid (records Close).
- Repeated `Close` does not append extra verbs.
- After `Close`, a new `MoveTo` starts a clean independent subpath.

No automatic geometry is inserted beyond the explicit Close verb. Consumers
that need a closing edge for rasterization will interpret Close later.

## Ownership / lifetime

| Rule | Behavior |
|---|---|
| Create | `robo_gfx_path_create` — owned path, refcount 1 |
| Retain / release | Same pattern as surfaces |
| Storage | Path owns command array via RoboMem |
| `reset` | Clears commands; keeps capacity |
| `clear` | Clears commands and frees capacity |
| Threading | Not concurrently mutable unless the caller synchronizes |

No global path state. No dangling references into caller memory.

## Iteration

| API | Role |
|---|---|
| `robo_gfx_path_command_at` | Random access by index |
| `robo_gfx_path_iter_begin` / `_next` | Sequential read-only walk |

Order is append order. For MOVE_TO / LINE_TO, `point` is defined. For CLOSE,
ignore `point`.

## Bounds

Axis-aligned half-open rect covering all MoveTo/LineTo integer points:

- Empty path → empty rect, `ROBO_OK`
- Single point → `1×1` covering that pixel
- Close adds no new extrema (start already present via MoveTo)
- Exact (not approximate); no curve extrema yet
- If exclusive right/bottom cannot fit `int32` (e.g. point at `INT32_MAX`)
  → `ROBO_INVALID_ARGUMENT`

## API (canonical)

| Symbol | Role |
|---|---|
| `robo_gfx_path_create` | Empty owned path |
| `robo_gfx_path_reset` / `_clear` | Reuse / shrink |
| `robo_gfx_path_move_to` | MoveTo |
| `robo_gfx_path_line_to` | LineTo |
| `robo_gfx_path_close` | Close |
| `robo_gfx_path_bounds` | Exact AA bounds |
| `robo_gfx_path_iter_*` | Read-only iteration |

## Errors

| Condition | Code |
|---|---|
| NULL path / out | `ROBO_INVALID_ARGUMENT` |
| LineTo without open contour | `ROBO_INVALID_ARGUMENT` |
| Growth OOM | `ROBO_OUT_OF_MEMORY` |
| Bounds overflow | `ROBO_INVALID_ARGUMENT` |

## Current limitations

- No quadratic/cubic construction or flattening
- No transforms, fill, stroke, clip, AA
- Independent of Surface / Bitmap / Framebuffer / line rasterization

## Deferred (later goals)

Transforms are Goal 6.9 (`Transforms.md`). Curve verbs + flatten, fill/stroke
rasterization, caps, joins, dashes, clipping engine, compositor / GPU.
