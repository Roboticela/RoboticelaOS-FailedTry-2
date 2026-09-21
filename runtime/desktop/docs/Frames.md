# Frames, Records and Presentation (Phase 11.13)

A frame is the unit of this module's work: the screen target is painted once, described once, and then
presented — or not — by the caller.

---

## 1. Three calls, and why not one

```c
robo_desktop_begin_frame(desktop, &diag);   /* open  */
robo_desktop_paint(desktop, &diag);         /* draw  */
robo_desktop_end_frame(desktop, &record);   /* close + describe */
```

The same triple RoboWindow uses, for the same reason: a **record** of the frame is worth having, and a
record can only be honest if the caller controls where the frame starts and stops. `robo_desktop_render`
is the one-call shorthand for a caller with nothing to interleave, and `desktop.frame.sequence` asserts
that the shorthand's record is **equal** to the sequence's — a shorthand that could drift from what it
abbreviates would be a second behaviour rather than an abbreviation.

Refusals, each of them a statement about a sequence:

| Situation | Result |
|---|---|
| `paint` or `end_frame` with no open frame | `ROBO_INVALID_ARGUMENT` |
| `begin_frame` while a frame is open | `ROBO_BUSY` — nested, never silently accepted |
| `paint` twice in one frame | `ROBO_BUSY` — a frame painted twice would composite every translucent thing twice, and only a watched counter would say so |
| `left` open, never ended, then resized | the frame is **abandoned** by `resize` (its record described a screen that no longer exists) |

An **unpainted** frame — `begin_frame`, `end_frame`, no `paint` — is legal and produces a record of
nothing (`fills == 0`, `windows == 0`), counted in `stats.frames_without_windows`. That is different
from a frame painted over a desktop with no windows, which still fills the surface and the panel.

A frame that **fails while painting a window's contents** still ends, and `robo_desktop_render` ends it
even on that failure — otherwise a caller would be left holding a desktop whose next `begin_frame`
reports `ROBO_BUSY` for a frame nothing will ever close.

---

## 2. The paint order

`robo_desktop_paint` draws in exactly this order, and the record's counters are the code's own
arithmetic:

1. **the desktop surface**, over the whole screen — the frame's explicit baseline and, being an opaque
   fill, its clear;
2. **the panel**: its body, then its last row as the hairline, then the two falloff rows below it, then
   the identity mark, then the three labels (identity, applications, status — the status last within the
   panel);
3. **each window in z-order** (back to front), and within a window:
   1. the border and the body (the body is what the contents are composited over),
   2. the **contents**: the client target cleared to the window's body colour, the display list
      executed into it, the result composited into the screen at the client rectangle's origin,
   3. the **close button**: its fill, then its cross as two strokes,
   4. the **title** text.

Nothing else. There is no shadow under a window, no animation, and no compositing of anything over the
windows except their own chrome — a menu bar, a tooltip or a task switcher would each be a layer, and a
layer tree is a **0** in the capability table.

Two properties of this order are worth stating because they are load-bearing rather than incidental:

- **a window's contents cannot reach the shell.** The document is rasterised into the window's own
  target, so a `push-clip` or an `opacity` scope inside it is bounded by that target's rectangle. A
  document cannot paint over the panel;
- **a document that fails leaves a window, not a hole.** The body was painted before the contents were
  composited, so a broken page shows an empty window body — and the failure is *reported*
  (`ROBO_NOT_SUPPORTED`, `WINDOW` domain, with the renderer's own message travelling) while the rest of
  the frame is still painted, because a frame that stopped there would leave a half-drawn screen whose
  record described less than the shell had done.

---

## 3. The frame record

```c
typedef struct robo_desktop_frame_record {
    uint32_t width, height;      /* the target's size */
    size_t fills;                /* shell fills executed */
    size_t labels;               /* chrome labels drawn */
    size_t labels_unshaped;      /* labels a missing face made impossible */
    size_t windows;              /* windows painted: visible, open */
    size_t windows_total;        /* windows the shell holds, closing and hidden included */
    size_t content_runs;         /* display-list runs executed */
    size_t content_pixels;       /* pixels of contents composited */
    size_t clipped;              /* shell drawings the screen's bounds cut */
    size_t bytes;                /* width * height * 4 */
    uint64_t checksum;           /* the target's checksum after the frame */
} robo_desktop_frame_record_t;
```

The counts are the two halves a reader compares: what the **shell** drew (`fills`, `labels`, the one
accent mark) and what the **windows** contributed (`windows`, `content_runs`, `content_pixels`).
`labels` beside `labels_unshaped` is the pair that lets a caller tell "this desktop has no text in it
*because there is no font*" from "this desktop decided not to draw any".

**`robo_desktop_frame_record_is_valid` is a closed rule**, in the same spirit as
`robo_render_frame_is_valid`: a zero or oversized dimension, `windows > windows_total`,
`content_pixels > width * height`, or a `bytes` that disagrees with the size — each is refused. A record
whose own fields disagree is a record assembled by hand, and the door says so rather than describing
something that never happened.

`robo_desktop_frame_record_equal` compares every field, including the embedded checksum, which is what
makes `desktop.frame.sequence`'s shorthand check a comparison of *pictures* and not just of counters.

The record is held **inside the shell** between `paint` and `end_frame`, so it is not a value a caller
can hold and write in between. `end_frame` folds it into the shell's statistics and hands a copy out.

---

## 4. Statistics

`robo_desktop_stats` reports what the shell has done since creation: `frames`,
`frames_without_windows` (the "is a desktop without applications still a desktop?" case),
`content_runs`, `content_pixels`, `labels`, `labels_unshaped`, `fill_drawings`, `clipped_drawings`,
`presentations`, `resizes`, `windows_added`, `windows_destroyed`, `windows_closed`, `pointer_actions`,
`pointer_moves`, `last_frame_checksum`.

Two of those need a sentence:

- **`presentations` counts the window path only** (`robo_desktop_present`), because that is the path with
  a backend behind it. A `present_to_surface` copy is a caller's own handoff and is described by the
  `robo_render_present_record_t` it returns, not by a shell counter.
- **the statistics are folded in at `end_frame`**, so a caller that never ends its frame gets none. That
  is the honest reading of "the frame never finished", not an oversight.

Every other counter in a frame's record is derived from the frame's own drawing; nothing here keeps a
second authority on a fact the window list already answers (the dump's visible-window count is computed
from the list, for instance).

---

## 5. Determinism, and how to check it rather than believe it

A frame is a function of the target's size, the theme, the metrics and the windows. Nothing in the paint
path reads a clock, an address, an allocation order or a hash iteration order.

Three doors make the claim checkable:

| Door | What it gives |
|---|---|
| `robo_desktop_frame_bytes` | the whole contiguous buffer, read-only |
| `robo_desktop_frame_checksum` | an FNV-1a over those bytes, valid whether or not a frame was just painted |
| `robo_desktop_frame_surface` | the pixels as a RoboGraphics surface, for a screenshot, a bit-level read or a presentation |

`desktop.frame.determinism` is the strongest form available: two **independently created** desktops, each
with a window whose contents came from the same markup, rendered and compared **byte for byte** — with
the record and the checksum compared beside them, because two pictures that match while their records
differ would mean one of the two is describing something else.

The checkerboard of what a caller must *not* expect: the same desktop painted twice without any state
change produces the same bytes (a frame does not accumulate), and a desktop that was resized is a new
picture (the target is reallocated and zero-filled, so the old pixels are gone by design).

---

## 6. The dump

`robo_desktop_write` produces **seven lines**, one per group of facts, each ending in a newline:

```text
desktop 1280x800 panel=0,0,1280,30 work=0,30,1280,770
  windows=1 visible=1 focused=3
  frames=4 without_windows=1 resizes=1
  fills=42 labels=6 unshaped=0 clipped=0
  content_runs=3 content_pixels=307200 presentations=3
  pointer=12,40 inside=1 pressed=0 area=window-content
  checksum=2f1c9e0b7a4d3355
```

The grouping is the **reader's** rather than the struct's: a caller comparing two desktops compares
*lines*, so a resize shows up in the first line, a pointer move in the sixth, and a repaint in the
seventh.

Three rules, and all three are why this is a door rather than a `printf`:

- **no address, ever** — asserted by `desktop.dump` in the form `=0x`, which is what a printed pointer
  looks like (an assertion written as `strstr(text, "0x")` was the bug 11.12's own dump caught, one
  module down);
- **the needed length is always reported** — a short buffer returns `ROBO_NOT_SUPPORTED` with
  `out_length` set to what the text *would* have taken, which is why the writer keeps counting after it
  stops storing;
- **it reads through public doors only** — this is the one file in the module that includes the front
  door rather than the private header, because a dump that could see the shell's fields would be a second
  place the shell's state has to stay true to.

`desktop.dump` asserts the seven lines **exactly** on a fresh desktop. That is deliberately brittle: the
dump is the door a developer reads when nothing else works, and a field that moves between two lines
changes what a diff shows. A case that pattern-matched the text would pass while the layout drifted.

---

## 7. Resize inside the frame story

`robo_desktop_resize` is documented in `desktop/docs/Geometry.md` §8. What belongs here is its effect on
frames: a resize **abandons a frame in flight** (clearing the open flag and the painted flag), because
that frame's record described a screen that no longer exists. A caller that resizes mid-frame gets a
clean sequence rather than a `ROBO_BUSY` it cannot clear.

The target's pixels are not carried over — the old frame was the wrong shape — so the next frame paints a
whole picture from the target's zero state. There is no partial repaint anywhere in this module:
`tiles` and `damage_tracking` are both **0**, and a frame is a frame.

---

## 8. Presentation

Two doors, one rule: the destination is the caller's, and the pixels are the frame's
(`render/docs/Presentation.md` is the other half of this phase).

```c
robo_result_t robo_desktop_present_to_surface(const robo_desktop_t *desktop, robo_gfx_surface_t *dest,
                                              int32_t x, int32_t y,
                                              robo_render_present_record_t *out, robo_desktop_diag_t *diag);

robo_result_t robo_desktop_present(robo_desktop_t *desktop, robo_render_present_record_t *out,
                                   int *out_presented, robo_desktop_diag_t *diag);
```

`present_to_surface` is a thin call of `robo_render_present` — the same door a window path uses, which is
why a guest build handing a frame to a wrapped display framebuffer and a test checking a copy are the
same code path with different destinations. The renderer's message **travels** through the desktop's
diagnostic: a desktop-level message would only repeat "presentation failed" in place of the reason.

`present` is RoboWindow's own frame triple around that call — `robo_window_begin_frame` (which gives the
window's owned render surface, sized to its *physical* client size), the presenter, `robo_window_end_frame`
(which marks the window dirty) and `robo_window_present_frame` (which reaches the backend: a Win32 client
area, a QEMU framebuffer). The window's frame is ended even when the copy failed, so the next iteration
can begin another one instead of being refused as nested.

What a caller must know, and what the doors say rather than imply:

- **nothing is paced.** No vertical blank, no double buffering, no frame queue, no "previous frame".
  `vsync` and `frame_scheduling` are **0**;
- **a frame that does not fit is clipped, not refused**, and the record says so
  (`presented_pixels + clipped_pixels == the frame's pixel count`). A window that shrank between the
  rasterise and the present must show the part of the picture that still fits, and the caller's next
  `robo_desktop_sync_size` closes the gap;
- **a desktop with no attached native window is not broken**: `present` answers `ROBO_NOT_SUPPORTED` with
  a `SURFACE` diagnostic, `present_to_surface` still works, and the frames are still real. That state is
  what a headless build and every suite in this module use;
- **`presenting twice is presenting twice`**: the door has no "already presented" state, because the
  pixels are the same pixels and the caller is the one that knows whether the destination still holds the
  previous frame's content.

`desktop.present.end_to_end` is the phase's capstone at the pixel level: markup through the whole chain
into a window's contents, the shell's frame composited under it, and the whole thing presented into a
destination surface whose pixels are then asserted — including that the shell's chrome landed where the
geometry said it would.
