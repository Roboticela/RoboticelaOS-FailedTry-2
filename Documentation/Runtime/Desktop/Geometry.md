# Geometry, Hit Testing and Resize (Phase 11.13)

Everything in this document is arithmetic on a size and a rectangle: no display, no window, no frame, no
document. That is the phase's requirement made checkable — *the desktop knows its screen, its panel and
its work area, and it does not hard-code a resolution* — and the way a requirement like that becomes a
fact rather than an intention is for the arithmetic to be asserted at several sizes, including one where
the panel eats most of the screen.

`desktop/tests/SuiteRoboDesktopGeometry.c` is the suite; its seven cases are a size and a rectangle each.

---

## 1. The coordinate statement

Screen coordinates are **device pixels**, origin at the display's top-left, `x` growing right, `y`
growing down, rectangles **half-open** (`x` inclusive, `x + width` exclusive). That is RoboGraphics'
convention, taken rather than restated, so a rectangle computed here can be handed to a fill, a
composite or a presenter without a conversion step that could disagree with any of them.

There is no device-pixel-ratio, no zoom and no per-monitor scale in this build. A DPR would break the
identity "paint coordinates are device pixels", which is why it is named as absent (§9) rather than
assumed to be 1 forever.

---

## 2. The four rectangles, and the three areas inside one of them

| Rectangle | Definition |
|---|---|
| `screen` | `(0, 0, width, height)` — the whole surface |
| `panel` | `(0, 0, width, panel.height)` — the top band |
| `work_area` | `(0, panel.height, width, height - panel.height)` — everything below the panel, and the only region a window is placed in |
| `identity` | A band at the panel's left: from `identity_inset` to one third of the panel's width, full panel height |
| `applications` | The **whole panel band**. It is narrowed to the measured label when a caller has a width (see §3) |
| `status` | A band at the panel's right: from `width - third - label_inset` to `width - label_inset` |

The three label areas are in the *geometry* rather than computed at paint time for one reason: a hit
test ("which part of the panel was clicked?") and a paint ("where does the clock go?") must not be able
to disagree about where the middle of a panel is.

They are laid out on **thirds** of the panel's width rather than on measured widths because the areas
have to exist *before* a font does — `RoboDesktopGeometry.h` has no font, and a geometry that could only
be computed with one would make a face-less desktop unlayoutable. A caller that has measured its labels
narrows an area afterwards (§3).

`desktop.geometry.panel_areas` pins the three bands, including the case that matters: on a screen so
narrow that a third is smaller than the identity inset, the identity band **collapses to a zero width
rather than inverting**.

---

## 3. `DesktopGeometryCompute` — total, with three refusals

```c
Result DesktopGeometryCompute(GfxSize screen,
                                           const DesktopPanelMetrics *metrics,
                                           DesktopGeometry *geometry);
```

**Total for every size this build accepts**, in the strong sense: every rectangle it returns is inside
the screen by construction (the work area starts where the panel ends), so no caller can be handed a
rectangle that leaves the surface it describes. The work area is therefore **thin rather than negative**
on a screen that barely holds a panel — `desktop.geometry.small_screen` pins a 64×40 screen producing a
64×10 work area, and asserts the two collapsed label bands.

The three refusals are each a caller mistake rather than a desktop with nothing on it: a non-positive
dimension, a dimension above `RoboDesktopMaxDimension`, and a panel height that is negative, zero or
**not smaller than the screen**. Zeroing the output is not part of a failure — the doors are documented
as leaving `out` unspecified on refusal, and `DesktopGeometryInit` is the door that clears one.

Note what geometry deliberately does **not** decide: whether the result is *drawable*. A ten-pixel work
area is a rectangle and stays readable; the door that refuses a window in it is
`DesktopWindowAdd` (§6).

### `DesktopGeometryPanelArea` — narrowing to a measured label

```c
Result DesktopGeometryPanelArea(const DesktopGeometry *geometry,
                                               DesktopPanelArea area,
                                               int32_t text_width,
                                               GfxRect *out);
```

One function rather than three, because all three cases are "narrow the area to the text and put it
where that label belongs": **centring** for `applications`, **right-alignment** for `status`, and
**left-alignment at the mark** for `identity`. A caller that had to know which alignment belonged to
which label would be re-deriving the design each time.

`text_width == 0` answers the area unchanged. That is the "this build has no font" case, and returning
the *area* rather than an empty rectangle is what keeps the panel's reserved space stable whether or not
a face was supplied. A negative width is refused (an unknown area too, and a NULL output).

---

## 4. `place` — where a window goes when the caller does not say

```c
Result DesktopGeometryPlace(GfxSize frame_size,
                                          const GfxRect *work_area,
                                          GfxPoint *out);
```

Deterministic **centred placement, nudged up**: horizontally centred, vertically at **a third** of the
work area's height rather than its middle, because a window at the exact centre of a screen reads as
lower than it is — the eye weights the space below a window as heavier than the space above it.

Placement is **not validation**. A frame larger than the work area is not refused here: the answer is
the work area's origin, so a caller that places first and clamps afterwards (which is exactly the order
`DesktopWindowAdd` uses) gets a sensible start rather than an error to handle. A negative frame
dimension or a NULL argument *is* refused, and so is an empty work area — there is no "centre of
nothing".

---

## 5. `contain` — the window that must stay on the screen

```c
Result DesktopGeometryContain(GfxRect frame,
                                            const GfxRect *work_area,
                                            GfxRect *out);
```

Two different repairs, and **which one applies is decided by the sizes** rather than by the caller:

- a frame **smaller** than the work area is **moved** until it is inside — a window dragged past an edge
  is pulled back rather than shrunk, because shrinking something the user dragged is a different
  operation from the one they performed;
- a frame **larger** than the work area is **shrunk** to it — this is the resize path: a display that got
  smaller must not leave a window with no visible pixels, and the work area is the largest a window may
  be.

The result is re-placed when shrinking left it outside, so the answer is always inside `work_area` in
both axes. An **empty work area** produces an empty rectangle, which is the honest answer when there is
no work area to be inside of, and `desktop.geometry.contain` pins both repairs plus that case.

Clamping is applied by `DesktopWindowAdd` and `DesktopWindowSetBounds`, so a caller cannot
get a window outside the work area by asking for one; `desktop.window.add.minimum_and_bounds` pins the
two consequences (a 4000×4000 request is shrunk, a request past the right edge is moved).

---

## 6. The window-fit rule, and `RoboDesktopMinDimension`

`RoboDesktopMinDimension` (16) is the smallest work area a window may be **placed** in, tested on both
axes — a 400×4 work area is as unplaceable as a 4×400 one. `DesktopWindowAdd` refuses below it
with a `SHELL` diagnostic, because the alternative is placing a window whose decorations the shell cannot
draw; the diagnostic's domain is `SHELL` rather than `WINDOW` because the *screen* is the thing that is
too small.

`desktop.window.no_work_area` pins the rule **from both sides**: a 64×40 desktop (a 64×10 work area)
refuses and leaves the window list empty and the id untouched, while a 64×46 desktop (exactly 64×16)
accepts and produces one window. A rule asserted only on its failing side is a rule that could be a
different rule.

A window below its own **decoration** minimum is a different case, and the answer is the opposite one: it
is **grown** rather than refused, because that minimum is a property of the decorations the shell draws
rather than of the caller's request. `RoboDesktopWindow.c` states both rules together and why they
diverge from `WindowSetSize`, which rejects rather than clamps.

---

## 7. Hit testing — front to back, and the vocabulary is the point

```c
Result DesktopHitTest(const Desktop *desktop,
                                    GfxPoint point,
                                    DesktopHit *out);
```

The walk is **front to back**: a point over two windows belongs to the front one, which is the answer a
*user* would give, and a document-order walk would answer with the window underneath. Inside a window the
resolution order is **close button, then title bar, then client area** — the button sits inside the bar,
so asking the bar first would make the button unhittable.

The answer is a `DesktopHit`: the area, the window (or `RoboDesktopWindowIdNone`), the point
echoed, and `inside` — whether the point is inside the screen at all. A point outside the screen is
`RoboDesktopAreaNone` with `inside == 0` rather than an error: a pointer can leave a window, and that
is not a failure.

Only **visible, open** windows are considered. A hidden window keeps its bounds, contents and z-order and
is not painted, so it is not hittable either — a window a user cannot see cannot be the thing a click
landed on. The panel is tested after every window (windows can never overlap it: the work area excludes
it) and the background is what remains inside the screen.

---

## 8. Resize, and following a window

```c
Result DesktopResize(Desktop *desktop, int32_t width, int32_t height, ...);
Result DesktopSyncSize(Desktop *desktop, int *out_resized, ...);
```

`resize` is the whole sequence in one call: **a new target first**, then the geometry recomputed, then
every window re-clamped into the new work area, then each window's content target released. Ordering
matters and is stated in the code:

- the new target is allocated **before** the old one is destroyed, so a shell that cannot allocate keeps
  the screen it had rather than becoming a desktop with no pixels;
- releasing the content targets rather than recreating them is what keeps a resize of eight windows from
  allocating eight targets before anything is drawn — the next frame creates what it actually needs;
- the target's pixels are **not** carried over. A resized screen has no previous frame to show (the old
  one was the wrong shape), so the next frame begins from the target's zero state and paints a whole
  picture.

A **no-op resize is not a no-op**: an unchanged size recreates nothing and reports `RoboOk`, because a
caller polling a window's size every iteration must not force a reallocation every frame.

`sync_size` is the one call a driven shell makes each iteration: it reads the attached window's
**physical** client size and resizes only when it changed, reporting that through `out_resized` (1 when
the screen changed, 0 when it did not). No attached window is `RoboNotSupported` with a `SURFACE`
diagnostic — the desktop has nowhere to follow, which is a legitimate state rather than an error.

---

## 9. What this build's geometry deliberately does not have

No device-pixel-ratio, no zoom, no per-monitor scale, no multi-monitor arrangement, no virtual desktops,
no window snapping, no tiling, no drag, no resize handles and no animation. Each is absent by decision
rather than by omission: every one of them is a structure (a scale factor, a monitor list, a window
manager's state machine, a clock) and each is named as a **0** in the module's capability table with a
phrase saying what it would need. `desktop/README.md` lists them in one place.
