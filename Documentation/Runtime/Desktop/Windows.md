# Shell Windows (Phase 11.13)

A shell window is the desktop's own object: a frame with decorations, a title, a client area, and — the
whole reason it exists — **contents** that are a display list produced by the pipeline below.

This document is the model (what a window *is*). What a window *looks like* on a frame, and the order
the shell paints it in, is `desktop/docs/Frames.md`.

---

## 1. The object

```c
typedef struct desktop_window {
    DesktopWindowId id;              /* the token the desktop hands out */
    char title[RoboDesktopTitleMax];       /* copied UTF-8, or "" */
    GfxRect bounds;                   /* OUTER: decorations included */
    DesktopWindowState state;        /* OPEN | CLOSING */
    int visible;
    const PaintDisplayList *content; /* borrowed */
    FontFace *content_face;           /* borrowed; NULL means the desktop's chrome face */
    RenderTarget *target;             /* owned, lazily created, client-sized */
} desktop_window_t;
```

Two identities are involved in this phase and they are **never the same value**: a
`DesktopWindowId` is a token the desktop hands out, while a `WindowId` is RoboWindow's
and belongs to the platform. A desktop is usually attached to a native window and a shell window is
usually shown through one, but a desktop with no display at all is a legitimate desktop that still
paints, and a type that conflated the two would make that state unexpressible — the same argument
`RoboDesktopTypes.h` makes for keeping them apart.

Ids are handed out from a monotonic counter, and a value that would be `RoboDesktopWindowIdNone` (0)
is **skipped** rather than issued: the "none" token is not a window, and an id nobody can name is worse
than an id that is not consecutive. `desktop.window.add.defaults` and `desktop.window.capacity` assert
that every id in the list is distinct and non-none.

A slot is **zeroed before it is filled**, because the array is reused across additions: a `content` or
`target` left over from a destroyed window would be exactly the cross-window leak this module can least
afford.

---

## 2. The description record

```c
typedef struct DesktopWindowDesc {
    const char *title;          /* UTF-8, copied, bounded by RoboDesktopTitleMax */
    int32_t x, y, width, height;
    const PaintDisplayList *content;
    FontFace *content_face;
    int visible;
} DesktopWindowDesc;
```

A record by value with an `init` door, the shape every configuration record in this tree has, so a field
added later does not renumber a caller's initializer.

**A negative `x` or `y` means "place me"**: the desktop answers deterministically with
`DesktopGeometryPlace` and then clamps. That is not a convenience — it is the only state a
session's first frame has (nobody knows the work area before there is a screen), and it makes "show me a
window" a call a caller can make without a layout calculation.

`DesktopWindowDescValidate` checks what is checkable **without a desktop**: NULL, a negative
width or height, a dimension above `RoboDesktopMaxDimension`, a title longer than
`RoboDesktopTitleMax`. Everything that depends on the desktop (the work area, the capacity, the
decoration minimum) is decided by `add` itself.

---

## 3. Bounds: outer, and the client rectangle derived from it

`bounds` is the rectangle the **frame** occupies, decorations included — the convention every windowing
system this one has seen uses. The rectangle the contents are drawn into is

```
client = ( bounds.x + border,
           bounds.y + border + title_height + content_inset,
           bounds.width  - 2 * border,
           bounds.height - 2 * border - title_height - content_inset )
```

and `DesktopWindowClientBounds` is the door. The difference between the two is the **decoration
metrics**, not a constant, so a caller that restyles the decorations gets a consistent client rectangle
for free. A caller that wants a window's *contents* to land somewhere specific does the subtraction
itself with that door, because only the caller knows which of the two rectangles it meant.

The title bar (`DesktopChromeTitleBounds`) is the whole band between the borders, and the close
button (`DesktopChromeCloseBounds`) is `close_size` square, inset `close_inset` from the frame's
right edge and centred in the band.

---

## 4. The list, the z-order, and focus

**The list is the z-order.** `windows[0]` is furthest back and `windows[count - 1]` is in front; a frame
paints the array in order, and `raise` is one `memmove` of an element to the end. A per-window `z` field
was rejected because the two would have to agree: with an order field, "paint in z-order" is a sort whose
tie-breaking is a second decision and "raise" is a renumbering of everything the window moved past.

- **`raise` focuses as it raises.** On a desktop with no separate focus model, "the front window is the
  focused one" is the whole of it, and the two cannot drift. `focus` is the door that focuses *without*
  raising, for a caller that wants the accent border elsewhere.
- **A new window is focused**, and it is added at the **front**: "the newest window is the one being
  used" is the only defensible default when there is no window manager.
- **Removal repairs the focused index** rather than letting it dangle. A shell whose focus pointed at a
  destroyed window would paint no accent border and route a press to nothing, and neither failure is
  visible until somebody looks.
- **Capacity is a bound, not a growth**: eight windows. Adding the ninth is `RoboAlreadyExists` with a
  `WINDOW` diagnostic, and `desktop.window.capacity` fills the list to prove both the refusal and that
  every slot is addressable by `DesktopWindowAt` (which takes an **index**, not an id — its name
  says which of the two it is).

---

## 5. Contents: the seam, and the lifetime rule that comes with it

```c
Result DesktopWindowSetContent(Desktop *desktop,
                                              DesktopWindowId id,
                                              const PaintDisplayList *content,
                                              FontFace *content_face,
                                              DesktopDiag *diag);
```

`content` is a `const PaintDisplayList *`: **read through `const`, never rebuilt, never
retained** — and it must outlive the window's last paint. That is the same lifetime rule a RoboWindow's
borrowed present surface has, and it is stated on the door rather than left to be discovered. Nothing in
this module knows what a DOM node, a computed style or a box is: a window's contents arrive as commands,
which is what keeps the shell drawable when the document layer is broken, empty, or not compiled in at
all.

**A window without contents is a legitimate window.** It paints its body and decorations, and its record
says `content_runs == 0`. That is what makes the shell's own smoke test possible (a desktop with a window
and no document), and it is why `content` may be NULL rather than being a required field.

`content_face` is the face the contents' text runs are drawn with, and NULL means the desktop's own
chrome face. A desktop with **no face at all** draws no glyphs and counts them, exactly as
`RenderRasterize` states for its own face argument — see `desktop.frame.labels`, which asserts both
sides of that pair.

A window's client target is created lazily and recreated only when the client size changed, so a resize
of a full desktop allocates nothing until the next paint (see `desktop/docs/Architecture.md` §1).

---

## 6. Closing is a request, not a destruction

```c
Result DesktopWindowRequestClose(Desktop *desktop, DesktopWindowId id);
```

It marks the window `CLOSING`, stops painting it, and leaves it in the list. **The owner destroys it.**
The close button on a window's title bar produces this and nothing else.

The middle state is the interesting one, and it is a decision about *who owns a window's lifetime*: a
caller that keeps state per window (an application, an open document, an unsaved edit) is told **before**
the object vanishes rather than after. A desktop that destroyed a window because somebody clicked its
close button would take that decision away from the only layer that can make it.

`DesktopWindowDestroy` is the removal, and it is the same call for an open and a closing window —
`desktop.window.close_and_destroy` pins the sequence, including that closing twice is idempotent and that
focus falls to the front remaining window.

---

## 7. Visibility

`set_visible`/`is_visible` are independent of the state: a hidden window keeps its bounds, contents and
z-order and is **not painted**, and it is not hittable either (`desktop/docs/Geometry.md` §7). A hidden
window is also not counted in a frame's `windows`, while `windows_total` — the denominator that counter
is read against — counts everything the shell holds, closing and hidden included. That pair is how a
caller tells "my desktop has no windows" from "my desktop's windows are not on screen".

---

## 8. What the pointer does to a window

`DesktopObservePointer(desktop, point, pressed, diag)` is the whole of this build's input
integration, and it is deliberately this small: it records where the pointer is and what is under it, and
on a **press** it does exactly one of two things —

- a press on a window's **title bar or client area** raises and focuses that window;
- a press on a window's **close button** requests that window's close.

`stats.pointer_actions` counts the presses the shell acted on and `stats.pointer_moves` the moves
observed; `DesktopPointer` reports the last state (the point, `inside`, `pressed`, the area and the
window).

**Nothing is delivered to a window's contents.** Hit-test delivery to applications is a later phase's, and
a shell that invented a delivery protocol now would be inventing the ABI those phases have to agree on —
which is why `input_delivery` is a **0** in the capability table rather than a half-built routing table.
A press that hit nothing is a successful call: pressing the background is a legitimate press that this
build simply has no meaning for.

---

## 9. What a shell window is not

Not draggable, not resizable from its edges (there are no handles — `set_bounds` is the programmatic door
and clamping is the only automatic repair), not animatable, not minimizable or maximizable, and not
iconifiable. There is no hover state, no per-window menu, no window menu and no decoration theme engine
beyond the metrics record. Every one of those is a **0** in the capability table (`window_manager`) or a
named absence in `desktop/README.md`, and the phase's own smoke test — a desktop with a window and no
document — is the state that proves the model does not secretly depend on any of them.
