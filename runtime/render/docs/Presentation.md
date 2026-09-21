# Presentation (Phase 11.13)

```text
frame (owned pixels)  →  destination surface  →  the window / display path
```

11.12 ended in a buffer and said so: `robo_render_target_surface` was named as *the door* for
presentation and the work was left to this phase. This document is that work, and it is deliberately the
smallest thing that can be called presentation rather than a compositor or a swap chain.

`RoboRenderPresenter.h` is the surface; `RoboRenderPresenter.c` is one rectangle of arithmetic and one
composite call. `ROBO_RENDER_PRESENTATION_IMPLEMENTED` is **1**, `presentation` in the capability table
is **1**, and `compositor`, `tiles`, `damage_tracking`, `frame_scheduling` and `gpu` are still **0** —
which is what this document is mostly about.

---

## 1. Why the door takes a surface and not a window

The destination is a **write-capable RoboGraphics surface**, the narrowest type that can mean "somewhere
these pixels could go": a window's owned render surface, a display framebuffer wrapped by its own module,
an offscreen surface in a test, or the intermediate a later compositor would use.

Taking a **window id** would make this module depend on RoboWindow — which `Assert-RoboRenderBoundaries`
refuses on purpose, because a rasteriser that could reach a window could not be tested without one.
Taking a **QEMU framebuffer** would make it depend on RoboDisplay for the same wrong reason. A frame is
pixels; where they land is the caller's choice, and the two callers this build has (the desktop's window
path and the guest's framebuffer path) each reach this door with the surface their own backend gave them.

The framebuffer *shape* is still served, one door over: `robo_render_present_framebuffer` takes
RoboGraphics' own `robo_gfx_framebuffer_t` and wraps it in a **borrowed surface for the duration of the
call only** — so nothing here outlives the call, the caller keeps its mapping, and a framebuffer whose
storage went away remains the caller's problem in exactly the way it was before the call.

---

## 2. The three doors

```c
/* Where would it land? No pixels written. */
robo_result_t robo_render_present_region(const robo_render_target_t *frame,
                                         const robo_gfx_surface_t *dest,
                                         int32_t x, int32_t y,
                                         robo_layout_rect_t *out,
                                         robo_render_diag_t *diag);

/* Present it. */
robo_result_t robo_render_present(const robo_render_target_t *frame,
                                  robo_gfx_surface_t *dest,
                                  int32_t x, int32_t y,
                                  robo_render_present_record_t *out_record,
                                  robo_render_diag_t *diag);

/* The same, into a RoboGraphics framebuffer. */
robo_result_t robo_render_present_framebuffer(const robo_render_target_t *frame,
                                              robo_gfx_framebuffer_t *fb,
                                              int32_t x, int32_t y,
                                              robo_render_present_record_t *out_record,
                                              robo_render_diag_t *diag);
```

`present_region` is the *question* rather than the act, and it is a door rather than a comment because
three different callers ask it: one that keeps a dirty rectangle, one that wants to skip a frame whose
target is off the destination entirely, and one sizing an intermediate. It returns an **empty rectangle**
when the frame and the destination do not meet, which is a legitimate answer rather than an error.

---

## 3. Clipping, not refusal — and a record whose arithmetic is checked

A frame that does not fit its destination is **clipped**: the rectangle outside the destination is
dropped and the rest is written. That is this tree's rule for every drawing (`robo_gfx_raster_fill_rect`
clips, `robo_gfx_surface_composite_rect` clips, `robo_paint_decoration_paint_bounded` clips), and it is
the rule a resize needs: a window that shrank between the frame being rasterised and the frame being
presented must show the part of the picture that still fits rather than fail or blank itself.

What would *not* be acceptable is silence about it, so the record carries both halves:

```c
typedef struct robo_render_present_record {
    uint32_t frame_width, frame_height;    /* what was presented */
    uint32_t dest_width, dest_height;      /* where it went */
    robo_gfx_pixel_format_t dest_format;
    robo_layout_rect_t presented;          /* the destination pixels written */
    size_t presented_pixels;               /* inside `presented` (0 when the frame missed) */
    size_t clipped_pixels;                 /* frame pixels that fell outside */
    int32_t offset_x, offset_y;            /* the origin as *requested* */
    int complete;                          /* every frame pixel landed */
    uint64_t frame_checksum;               /* *what* was presented, not where it went */
} robo_render_present_record_t;
```

`presented_pixels + clipped_pixels` is **exhaustive**: with a valid frame and destination the two sum to
the frame's own pixel count, so a caller can check the arithmetic against
`robo_render_target_pixel_count` rather than trust it.

`robo_render_present_record_is_valid` is the **closed** rule, in the same spirit as
`robo_render_frame_is_valid`:

- a zero or oversized frame dimension is invalid;
- a `dest_format` that is not this build's target format is invalid — a record describing a presentation
  *out of* something this module cannot make is describing something that cannot exist;
- `presented` must be a real rectangle;
- the two counts must sum to the frame's pixels, and `presented_pixels` must not exceed the rectangle it
  was given;
- `complete` must agree with `clipped_pixels == 0`.

A record two fields of which disagree is a log that lies, and this is the door that says so.

`offset_x`/`offset_y` echo the origin **as requested** rather than as clamped, because `presented`
already reports where the pixels went: a record that clamped the offset and reported the clamped one
would hide the caller's mistake instead of describing the result.

---

## 4. One authority for where a pixel lands

`robo_render_present` computes the destination rectangle once (through `present_region`) and then hands
the **frame's own bounds** to `robo_gfx_surface_composite_rect` rather than the intersection it just
computed. That looks like doing the work twice and is the opposite of it:

- the composite call applies the clip, so there is **one** authority for where pixels land — the module
  that writes them;
- the intersection is used only for the **record**;
- a presenter that pre-clipped its source rectangle would be re-deriving the composite's rule, and the
  two would drift the first time a destination had a stride, a format, or a bounds rule this file did not
  think of.

There is also **no format conversion of its own**: a BGRA8 destination is served by the graphics module's
existing conversion and a format that module cannot write is refused rather than guessed at.

---

## 5. The dump

`robo_render_present_write` produces **four** lines, one per group of facts:

```text
present 1280x800 RGBA8 -> 1280x800 RGBA8 at 0,0
  presented=0,0,1280,800 pixels=1024000 clipped=0 complete=1
  frame_checksum=2f1c9e0b7a4d3355
  record=valid
```

No `stdio`, **no address anywhere in the text**, and a buffer too small reports the length the caller
would have needed instead of truncating — the same shape `robo_render_frame_write` established one
release earlier, including the fact that the last line is the record's own validity as this module reads
it.

`ROBO_RENDER_PRESENT_DUMP_LINE_MAX` (160) is one line's worst case, so a caller can size a buffer without
guessing.

---

## 6. Nothing is retained, and there is no lifecycle

The frame is read; the destination is written; the call returns. There is no "last presented frame", no
dirty flag, no queue and no partial frame — which is why this door needs no `init`, no `shutdown` and no
`is_initialized`, the same argument `RoboRender.c` makes for the module as a whole, one level down.

Two consequences a caller must know, and the doors state both rather than imply either:

- **the frame is not consumed, cleared or modified**, and `presenting twice is presenting twice`: this
  door has no "already presented" state, because the pixels are the same pixels and the caller is the one
  that knows whether the destination still holds the previous frame's content;
- **this door does not roll back.** A composite that fails partway leaves the destination holding what
  the graphics module wrote before it failed, and the record describes what the *call* did — saying so is
  better than implying an atomicity the module below does not offer.

---

## 7. The error model

| Condition | Result |
|---|---|
| NULL frame or destination, or a read-only destination | `ROBO_INVALID_ARGUMENT` |
| a destination format this build cannot write | `ROBO_NOT_SUPPORTED`, `FRAME` domain, with a message |
| a frame that missed the destination entirely | **`ROBO_OK`** — a successful presentation of nothing, described by the record |
| the composite itself failed | `ROBO_ERROR` with a diagnostic, destination left as the module below left it |

The third row is the one worth reading twice: "nothing landed" is not an error, because a caller that
presented a frame whose destination moved out from under it needs the *record*, not an exception to
handle.

The diagnostic vocabulary did **not** grow: a presentation failure is a `FRAME` failure, because that is
the stage — a frame that could not be run or could not be composited — and a `PRESENT` domain would send
a reader to a module that does not own the pixels. `render.conformance.presenter` asserts the shape of
that claim rather than exercising it: the door exists and reports its flag, `presentation` is 1 while
`compositor` is still 0, and the domain list is unchanged.

---

## 8. What this is not

- **Not a compositor.** One frame, one destination, one offset, source-over. No layer order, no opacity
  group, no damage union, no notion of "the previous frame". `compositor` stays 0 beside `presentation`,
  and `render.conformance.presenter` asserts that pair deliberately: a build that had conflated "a frame
  can reach a destination" with "more than one surface has an order" would present the second as the
  first.
- **Not VSync, and not pacing.** The call is synchronous and the pixels are in the destination when it
  returns. There is no VBlank signal, no frame queue and no deadline.
- **Not a swap chain.** One destination, the caller owns it, this module retains nothing after the call.
- **Not a format converter.** It uses the graphics module's own conversion and refuses what that module
  cannot write.
- **Not an allocator.** No intermediate buffer is created; the destination is the caller's.

Each of those is named as a **0** in `RoboRender.h`'s capability table with a phrase saying what it would
need, and `ROBO_RENDER_PRESENTATION_IMPLEMENTED`'s own release note states the width of the claim.

---

## 9. The two callers this build has

| Caller | Path |
|---|---|
| `RoboDesktop` | `robo_desktop_present_to_surface` (any writable surface, at an offset — the guest path and the suites) and `robo_desktop_present` (RoboWindow's frame triple around this door, ending in `present_frame` and a backend) |
| The guest's display path | a UEFI/QEMU mapping wrapped as a RoboGraphics framebuffer, presented through `robo_render_present_framebuffer` |

Both are the *same* call with a different destination, which is the property that makes the guest build
and the desktop build share one presentation rule instead of two. `render/docs/Tests.md` §1 lists the six
presenter cases; `desktop/docs/Frames.md` §8 states the desktop's side of the arrangement, including that
`stats.presentations` counts the window path because that is the path with a backend behind it.
