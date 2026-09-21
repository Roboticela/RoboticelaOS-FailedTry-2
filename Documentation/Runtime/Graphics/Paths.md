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
| `RoboGfxPathMoveTo` | point | Begin a new subpath; set current + contour start |
| `RoboGfxPathLineTo` | point | Append a straight segment (requires open contour) |
| `RoboGfxPathClose` | none | Close the open contour; current → contour start |

Reserved (not constructible in 6.8):

| Verb | Future use |
|---|---|
| `RoboGfxPathQuadTo` | Quadratic Bézier |
| `RoboGfxPathCubicTo` | Cubic Bézier |

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
- `LineTo` without an open contour → `RoboInvalidArgument` (path unchanged).
- `Close` with no open contour → `RoboOk` no-op (no verb appended).
- `Close` after MoveTo alone is valid (records Close).
- Repeated `Close` does not append extra verbs.
- After `Close`, a new `MoveTo` starts a clean independent subpath.

No automatic geometry is inserted beyond the explicit Close verb. Consumers
that need a closing edge for rasterization will interpret Close later.

## Ownership / lifetime

| Rule | Behavior |
|---|---|
| Create | `GfxPathCreate` — owned path, refcount 1 |
| Retain / release | Same pattern as surfaces |
| Storage | Path owns command array via RoboMem |
| `reset` | Clears commands; keeps capacity |
| `clear` | Clears commands and frees capacity |
| Threading | Not concurrently mutable unless the caller synchronizes |

No global path state. No dangling references into caller memory.

## Iteration

| API | Role |
|---|---|
| `GfxPathCommandAt` | Random access by index |
| `GfxPathIterBegin` / `_next` | Sequential read-only walk |

Order is append order. For MOVE_TO / LINE_TO, `point` is defined. For CLOSE,
ignore `point`.

## Bounds

Axis-aligned half-open rect covering all MoveTo/LineTo integer points:

- Empty path → empty rect, `RoboOk`
- Single point → `1×1` covering that pixel
- Close adds no new extrema (start already present via MoveTo)
- Exact (not approximate); no curve extrema yet
- If exclusive right/bottom cannot fit `int32` (e.g. point at `INT32_MAX`)
  → `RoboInvalidArgument`

## API (canonical)

| Symbol | Role |
|---|---|
| `GfxPathCreate` | Empty owned path |
| `GfxPathReset` / `_clear` | Reuse / shrink |
| `GfxPathMoveTo` | MoveTo |
| `GfxPathLineTo` | LineTo |
| `GfxPathClose` | Close |
| `GfxPathBounds` | Exact AA bounds |
| `robo_gfx_path_iter_*` | Read-only iteration |

## Errors

| Condition | Code |
|---|---|
| NULL path / out | `RoboInvalidArgument` |
| LineTo without open contour | `RoboInvalidArgument` |
| Growth OOM | `RoboOutOfMemory` |
| Bounds overflow | `RoboInvalidArgument` |

## Current limitations

- No quadratic/cubic construction or flattening
- No transforms, fill, stroke, clip, AA
- Independent of Surface / Bitmap / Framebuffer / line rasterization

## Deferred (later goals)

Transforms are Goal 6.9 (`Transforms.md`). Curve verbs + flatten, fill/stroke
rasterization, caps, joins, dashes, clipping engine, compositor / GPU.
