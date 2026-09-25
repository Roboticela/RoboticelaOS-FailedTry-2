# RoboCompositor — surfaces, layers, damage, frames (Phase 15)

> **One module decides what is on screen, in what order, where, and how much of it a frame has to
> produce. It never decides what a drawing command means.**

Phase 15 is RoboCompositor. It sits on RoboGraphics and nothing above it, it links no renderer, and
its whole model is one pipeline:

```text
Damaged regions  (a layer moved, a surface's content went stale)
    ↓
Tiles            (what unit of work a frame has)
    ↓
Jobs             (a budget it can be cut off inside, and resume next frame)
    ↓
Rasterize        ← the caller's `CompositorRasterFn` — RoboRender's business, not this module's
    ↓
Damage again     (a produced surface is not on screen until a composite places it)
    ↓
Layer tree       (rectangle, offset, scale, opacity, clip, visibility)
    ↓
Composite        (depth-first paint order, inherited opacity and clip, source-over)
    ↓
Present          (the whole composition, or only the damaged regions)
    ↓
Frame record     (what this frame cost, and the fence it completed)
```

## 1. The four rules

These are the whole module. Everything else is bookkeeping.

1. **One layer's content is a `GfxSurface` somebody else rasterized.** The compositor holds it, a
   layer refers to it by name (`CompositorSurfaceId`), and it composes it. It does not open a
   display list, does not know what a glyph is, and does not call a painter. `CompositorRasterFn` is
   the seam: this module says *what* region of *which* surface is stale, and the caller says how its
   pixels are produced.
2. **Paint order is the child list.** Depth-first, parent first, children in creation order. There is
   no z-index: an ordered list is an order only one caller can state, and a number is one two
   callers can disagree about.
3. **Opacity multiplies and clip intersects down the chain.** A fading window fades what is inside
   it; a clipped panel clips its rows. Both are computed on the way in, so no call site can forget
   one.
4. **A frame costs what changed.** Damage is a bounded list of `(surface, rectangle)`; a present may
   write only those regions, and the frame's record says how many pixels it wrote. A two-pixel edit
   is a four-pixel frame, and the suite asserts that as a number.

## 2. The layer tree

A layer is `{id, parent, surface, rect, offsetX/offsetY, scale, opacity, clip, visible}` plus the
two links that make the tree (`firstChild`, `nextSibling`; the roots are a list of the same shape).

The transform is one line:

```text
compositor = boxOrigin + offset * parentScale + p * scale
```

so a **translate happens before the scale** — an offset is not multiplied by the layer's own scale —
an ancestor's scale multiplies both the position and the size, and **a child is placed against its
parent's content origin**, which is what makes `offsetY` a scroll (the content and everything inside
it moves) rather than a nudge (only the layer's own pixels move).

`CompositorLayerBoundsInCompositor` is that expression with `p` at the origin and the size at the
rectangle's; the composite pass and the hit test both go through the same resolution, which is why a
scaled or clipped layer is answered identically in all three places.

## 3. Damage, and why over-merging is the right trade

A damaged region is `(surface, rect)`. `surface == none` means **the composition**: something moved,
the pixels are fine and only the arrangement is stale, so a present has work and the rasterizer has
none. A named surface means **that surface's content is stale**, which is what produces raster jobs.
One list, two spaces, because the two are one question — "what needs doing this frame" — with two
answers.

The list is bounded (`maxDamageRects`). Past the bound it never *drops* a region — a dropped region
is a visibly stale pixel — it does one of three things, in order:

1. **Unions** intersecting regions of the same space on insert (counted as `damageMerges`). This is
   also why `CompositorDamagePixelCount` is exact: entries in a space never overlap, so the count is
   a true sum.
2. **Merges** the entry whose union with the new region grows least, when the list is full.
3. **Becomes full** (`CompositorDamageIsFull`), which subsumes every composition region, or dirties
   every tile of the surface the region named. Both are counted as `damageOverflows`.

A merge is a bigger region than it needs to be — more work, never a wrong pixel — and that is the
trade this module makes on purpose. Every rectangle is clipped to the space it names before it is
stored, so a region that clips to nothing is a counted no-op success (`damageClippedAway`).

## 4. Tiles, jobs and the budget

A tile grid is created **lazily, per surface**, the first time that surface is damaged: a compositor
with one stale surface does not pay for a grid over every surface in the pool. Tiles are
`tileEdge` square with the right and bottom edges clipped to the surface, and each is `Empty`,
`Clean` or `Dirty`.

**Surface damage becomes dirty tiles when the damage is added, not when a frame is scheduled.** That
one ordering is what keeps a tile a budget cut from being re-rasterized on every pass until something
presents, and it is why the deferred case is repeatable: the frame after a cut finds exactly the
tiles that were cut.

`CompositorScheduleFrame(budget)` queues one job per dirty tile, row-major, smallest first, and stops
at the budget — reporting what it queued and what it left (`scheduled`, `deferred`).
`CompositorRunScheduled` calls `CompositorRasterFn` once per job and marks a tile `Clean` only when
the call answered `RoboOk`. A refusal **stops the run**: the job stays queued, the tile stays dirty,
and the count and the diag both say so — the next frame tries again, which is what a busy buffer is
supposed to mean. The queue is a ring, so a job that survives the frame boundary costs nothing.

A successful job invalidates every layer that uses the surface, in composition space. Without that
join a raster job would produce pixels nothing ever presented — which is the one bug in this pipeline
that looks like "the compositor works" while the screen never changes.

## 5. Surfaces, the pool and the budget

`CompositorSurfaceAcquire(id, size, format)` returns a `GfxSurface` and adds a reference;
`CompositorSurfaceRelease` drops one. A release **caches** rather than frees: the pixels are already
paid for, and a shell re-acquiring the same name next frame is the common case. `CompositorTrim`
returns them, least-recently-released first, and an acquire over `memoryBudgetBytes` calls it before
refusing.

Acquiring an existing name with a **different size or format** replaces the storage, bumps the
generation and drops the old pixels — the layers keep working because they hold the name, not the
storage. That is what `generation` is for: a dump can say whether the pixels under a name changed.

Destroying a surface is allowed and is not a dangling reference: every layer pointing at it is
detached (its `surface` becomes none) and the count is reported. The three refusals are three
counters, because they mean three different things to a caller: `refusalsOom` (the allocator said
no), `refusalsBudget` (the number was too small) and `gpuRefusals` (a feature this build does not
have).

## 6. Frames, the clock, the post queue and fences

One frame, in one order, once:

```text
posts drained → animations stepped → damage → tiles → jobs → composite → record → fence
```

`CompositorPumpFrame` is that order; `CompositorPresent` is its last three steps. A caller with its
own loop can drive the parts; a caller that wants one call gets the pump. Neither has its own copy of
the order.

**Threading is stated, not enforced by a lock.** A device thread may only call `CompositorPostFrame`
(it enqueues a reason and touches nothing else); the frame runs on one thread — whichever calls the
pump or the present — and is the only thread that mutates layers, surfaces, tiles or the tree. A lock
would make the *unsupported* shape (two threads pumping) look supported, and the supported shape
needs none.

**Vsync is a deadline on the caller's clock.** With an interval set, a pump before the deadline
**coalesces** (`framesCoalesced`) rather than drawing, and a pump after it draws and records the
frame as **late** rather than pretending the deadline moved. Because `CompositorAdvanceTime` always
leaves `nextVsync` in the future, the "a deadline passed" fact is a flag of its own — otherwise a
late frame would be answered "not yet" forever.

**A present that fails keeps its damage.** The list is cleared *after* the composite succeeds, never
before: a transient refusal must not consume the regions that still need a frame, or the screen
stays stale for good.

**Fences are the destination's pixels.** There is no swap chain here, so a present *is* the
destination and the fence is a number naming which present a caller has seen:
`CompositorWaitFence` answers `RoboOk` for a fence at or before the last completed present,
`RoboTimeout` for one ahead of it, and `RoboNotFound` for zero or an id past the counter. It never
blocks, because a CPU present is already done when it returns.

## 7. Animations and smooth scrolling

A track is `property(layer) = interpolate(from, to, easing(t))` over the caller's clock — five
properties (`opacity`, `offsetX`, `offsetY`, `scale`, `scrollX`, `scrollY`), three easings. A
property with an active track is **replaced** by a new one on the same property, so a drag that
becomes an inertia scroll is a continuation rather than a fight. A cancel leaves the property where
the last step left it (that is the honest answer for a released drag); reduced motion **snaps to the
end value in the same step** — the state arrives, it simply does not travel.

A smooth scroll is `ScrollX`/`ScrollY` with the flag that says why (`CompositorAnimationInfo::scroll`),
plus the damage of the layer's whole box: a scroll moves content across all of it, so damaging only
the distance travelled would leave a band of old pixels behind. `CompositorLayerSetScroll` remains
the door that *is* the state.

## 8. What this build deliberately does not have

Stated here and in `RoboCompositorVersion.h`, because a consumer must be able to refuse to build a
path on it rather than discovering the absence at runtime:

| Absent | Why, and where it would start |
|---|---|
| `gpuResources`, `gpuSync` | `CompositorResourceCreate` refuses the two GPU kinds **by name** and counts the refusal. The policy — who may allocate what, what a refusal means, how it is counted — is implemented; the allocator behind it arrives with a GPU backend, and that door is where it starts. |
| `doubleBuffering` | There is no swap chain; a present writes the destination the caller handed it. |
| `compositorThread` | 15.14's model is a **pump**: post from anywhere, pump from one place. What a thread would buy is parallelism; what a frame needs is ownership, and ownership is a rule rather than a second stack. |
| `rotation` | A quarter turn is a rasterizer's business (12.20's `GfxGlyphRasterParams` already has one), not a compositor's resample. |
| `subpixelSampling` | A scaled layer is sampled **nearest neighbour**, by integer arithmetic — exact at 1:1 and at every integer magnification, and never reading outside the source. |
| `colorManagement` | No ICC and no transfer-function conversion at composite time: the blend is the source-over RoboGraphics states, on stored values. |
| `presentToDisplay` | The destination is a `GfxSurface` the caller owns. Handing the composition to a framebuffer or a panel is `RenderPresentFramebuffer`/RoboDisplay's door, above this module. |
| `layerBlur`, `toneMapping` | Not implemented, and not faked. |
| Perspective, non-uniform scale, filters, backdrop effects | A layer has a translate, a uniform scale and an axis-aligned clip. |
| A real-time GPU frame budget | The performance counters are 15.17's baseline, not Phase 41's system. |

## 9. What a frame costs

15.17's counters are read where a frame is composed, not estimated: `CompositorFrame` carries the
frame's own `damagePixels`, `compositedPixels`, `presentedBytes`, `layersPainted`, `jobsRun` and
`tilesRasterized`, and `CompositorStats` accumulates them (with `compositedPixelsPeak` for the worst
frame). The point of `pixelsComposited` is that it is a **model**: under region presents the pixels a
frame writes are the damage and the layers over it, exactly, so a 2×2 change over one layer writes 4
pixels and a 16×8 change over two writes 256 — and that is what `compositor.performance.baseline`
asserts, twice, with the second pass at double the damage. It also prints the wall-clock cost of both
passes, which is the number a reviewer reads and nobody asserts: a timing threshold on a shared
development machine is a flake, while "the work is the damage" is a fact a build can be wrong about.

## 10. Diagnostics

Two rules, and they are the reason a compositor bug is findable at all:

**Complete or absent.** `CompositorDump` accumulates the length the text *needs* and appends only
while it fits. A buffer too small is emptied and its required length reported — never half a line,
because a truncated `opacity 2` reads as a value rather than as a failure. A NULL buffer is the
length query.

**One writer per view, and the summary composes them.** `CompositorWriteCapabilities`, `…Layers`,
`…Layer`, `…HitTest`, `…Damage`, `…Tiles`, `…Jobs`, `…Surfaces`, `…Animations`, `…Stats`,
`…Frame` each own a section, and `CompositorWriteSummary` calls them in order. A summary and a
single-section dump therefore cannot disagree about a number: there is one implementation of each.

A refusal is recorded **even when the caller passed no diag**: `CompositorGetLastDiag` keeps the most
recent one and `CompositorGetRefusalCounts` keeps a per-domain table. One record, not a log — this
tree has one `RoboLog`, and a second ring buffer of diagnostics would be a second place for a failure
to be.

`CompositorValidate` checks every invariant the module states (a parent that exists, a root list
that is not a cycle, a child that agrees about its parent, a positive finite scale, a surface that
exists, a reference count that is possible, no table holding more than it has slots) and
`CompositorFuzz` drives the public doors from arbitrary bytes with validity as the only invariant.
An invariant only the fuzzer knows about is an invariant nobody else can test, so the check is a
public door and the suite calls it too.

## 11. Tests

| Suite | Cases | What it proves |
|---|---|---|
| `Tests/Compositor/SuiteRoboCompositor.c` | 9 | The vocabulary and its positional order, the capability table's two blocks, configuration and construction, the clock's monotonicity, counters, per-domain refusals, the dump's completeness, and the fuzz invariant. |
| `Tests/Compositor/SuiteRoboCompositorVisual.c` | 20 | The tree (child order, reparenting, cycles, the layer limit), geometry (accumulated origin and scale, an offset that moves a subtree), paint order, hit testing through clips, the composite pass (destination size, opacity against the one blend, clip intersection, nearest-neighbour scaling, paint order, scroll), damage (clipping, merging, the full fallback, layer invalidation), the present (full and region, exact pixel counts, refusals that keep their damage) and 15.18's two-generation byte-for-byte determinism plus the `Build/Compositor-Conformance.png` artifact. |
| `Tests/Compositor/SuiteRoboCompositorScheduling.c` | 21 | The pool (tested, replaced, destroyed with detach), the budget and the trim, the resource policy's GPU refusals, tile geometry at the edges, dirty/clean, the scheduler with and without a budget (read off the surface's own pixels), a refusing callback that keeps its job, the easings, tracks (interpolate, replace, cancel, zero duration), reduced motion that snaps, smooth scrolling, frames (post coalescing, the pump's idle and posted cases, vsync coalescing and lateness, fences) and 15.17's baseline, which prints a frame's cost and asserts that the work is the damage. |

Run: `Scripts/Test/TestCompositor.ps1 -Target Host` (50 cases, `compositor.*`), and
`Scripts/Build/BuildCompositor.ps1 -Verify` for the module's own boundary scan (it may include
RoboGraphics and nothing above it). The whole-runtime gate is `Scripts/Test/TestRuntime.ps1`.

The artifact of record is `Build/Compositor-Conformance.png` — a scene with a clip, a scroll, a
translucent child and a scaled surface, rendered twice and compared byte for byte before it is
written. `ROBO_COMPOSITOR_PNG` relocates it, the way `ROBO_DESKTOP_PNG` does.

## 12. Where this sits in the tree

```text
RoboBase → RoboMem → RoboContainers
    ↘ RoboGraphics → RoboCompositor → RoboDesktop   ← Phase 15 + 15.19
    ↘ RoboGraphics → RoboDisplay → RoboWindow → RoboDesktop
    ↘ RoboFont → RoboText → RoboLayout / RoboPaint → RoboRender → RoboDesktop
```

RoboCompositor is a **leaf consumer of RoboGraphics** and a **producer for RoboDesktop**: the shell
wires it in `RoboDesktopCompose.c` — a wallpaper layer, a panel layer, one layer and one surface per
window, damage regions, and `CompositorPresent` into the screen target the caller owns. Composition
itself is **CPU** (`gpuResources`/`gpuSync` remain 0 — no GPU textures in the layer tree yet). The
**present** half is hybrid on the host: when Win32 reports `gpuPresent`, the composed CPU surface is
flipped through D3D11/DXGI; otherwise GDI BitBlt (and on QEMU, the linear FB copy). Display flip APIs
stay outside this module (`presentToDisplay` remains 0). The shell calls `CompositorPresent`
directly rather than `CompositorPumpFrame`; the pump and post queue are available for a later frame
loop that wants them. The artifact of record for the module itself is still
`Build/Compositor-Conformance.png`.
