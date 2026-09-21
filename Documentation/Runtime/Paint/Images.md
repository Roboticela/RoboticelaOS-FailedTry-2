# RoboPaint — image painting (Phase 11.7)

The fourth drawing kind, and the first whose appearance this module does **not** resolve. A
background's colour comes from a computed style, a border band's from a used record and a text
run's from a text node; an image's pixels come from a **resource** the document attached to an
element. That difference is the whole shape of the phase: the record decides *whether* there is a
drawing, RoboLayout decides *where* it goes, and RoboGraphics decides what a resampled pixel is.

```
HTML image element / its decoded resource
        ↓
RoboLayout            the element's content box
        ↓
RoboPaint             IMAGE record  — resource + source rectangle + snapshot facts
        ↓
display list          draw-image    — one destination rectangle, resolved
        ↓
RoboGraphics          GfxRasterDrawImage
        ↓
RoboWindow / QEMU
```

---

## 1. The resource model, and whose it is

An image element's resource is **RoboHTML's**, not this module's. `RoboHTMLImage.h` owns the
reference, the fetch seam, the decoder call and the failure *state*; `PaintImage`
(`RoboPaintImage.h`) owns only the outcome that is paintable: a decoded `GfxImage` and the
rectangle of it to draw.

| Layer | Owns | Answers |
|---|---|---|
| `RoboHTMLImage.h` | the `src`, the fetch, the decode, the resource's state (`NONE`/`PENDING`/`DECODED`/`FAILED`/`UNSUPPORTED`) | "does this element have a picture, and is it usable?" |
| `PaintImage` | an identity, the resource's dimensions and format, and a source rectangle | "what would be drawn?" |
| the paint list / display list | one **reference** each | "how long does the picture live?" |

Nothing here fetches, decodes, sizes, caches or discovers. There is no URL, no byte buffer, no
loader and no `srcset` in this module — a reference is a *document* fact and this layer only ever
sees its outcome.

## 2. The record

A paint operation is a value record whose meaning varies by kind. `IMAGE` is the fourth field on
it (after the border style of 11.5 and the text run of 11.6), and the shape of the payload is the
phase's one structural decision: **an image record names a resource and a rectangle; it never
names a document.**

| Field | Why it is here |
|---|---|
| `resource` | the decoded pixels, borrowed by the record and **retained by whoever stores it** |
| `identity` | RoboGraphics' own id for the resource, copied at snapshot time — what two records are compared by, **never the address** |
| `width`, `height`, `format` | the resource's own facts, copied rather than re-queried: a dump, a validity rule and a consumer all want them |
| `source` | the rectangle of the image that is drawn, in **image pixels** — the full image in this build |

The record carries no destination rectangle, and its absence is not an omission: the operation
already carries the box's three rectangles (`border`, `padding`, `content`), and for a **replaced
element** the destination *is* its content box — CSS paints a replaced element's content inside
its content box, with the element's padding and border around it. So the destination is the same
number every other record's geometry is, and the display builder resolves it into the command's
one rectangle exactly as it resolves a border side's band. A fourth rectangle on the record would
be a second answer to a question layout has already answered.

## 3. Where an image record sits in the sequence

```
#0003   begin-box      node=2 depth=1 ...            the <div class="box">
#0004   background     node=2 depth=1 border=0,0,200,100 ...
#0005   border-top     node=2 depth=1 ...
...
#0009   content        node=2 depth=1 ...            the box's own painting is over
#0010   begin-box      node=3 depth=2 ...            the <img>
#0011   image          node=3 depth=2 ... src=0,0,4x4 source_size=4x4
#0012   content        node=3 depth=2 ...            the image *is* this box's content
#0013   end-box        node=3 depth=2 ...
#0014   end-box        node=2 depth=1 ...
```

Three facts about that placement, each of which is a decision:

- **It is inside the image element's own group**, at that box's depth and naming that box —
  because `<img>` has no object of its own; the element's box *is* where the picture goes.
- **It is after that box's own decorations and before its `CONTENT`** — CSS's order for a
  replaced element is background, border, content, descendants.
- **It is the opposite placement from a text run**, which is emitted a level *below* its box
  because a run **is** an object and the box it belongs to is its parent. The two drawing kinds
  that carry no colour therefore reach the stream at two different depths, and both because the
  tree says so rather than because a painter chose.

## 4. The display command

`RoboPaintDisplayCmdDrawImage` carries the resource, the source rectangle and the resource's
facts — plus the command's own resolved `rect`, which is the **destination**.

The conversion is one line of derivation and one line of payload:

```
out->kind  = DRAW_IMAGE;
out->rect  = operation->boxes.content;      /* the destination */
out->image = operation->image;              /* borrowed; the list retains it */
```

The command's `color` stays `none`, and the validity rule is stated on a **new predicate** rather
than on `draws`: `PaintDisplayCommandKindUsesColor` names the kinds whose appearance *is*
a colour (the background, a border band, a run). An image is a drawing that is not one of them, so
the rule "a kind that uses a colour must carry a drawable one, and every other kind must carry
`none`" holds without an exception list — and the next payload kind (a surface, a gradient, a
video frame) answers no there rather than being added somewhere.

## 5. RoboGraphics: one new primitive

`GfxRasterDrawImage` (RoboGraphics 0.17.0) is the *scaled* half of Goal 6.6's 1:1
`GfxSurfaceCopyRect`. It lives in RoboGraphics rather than here because pixels are that
module's business: the clip, the source-over blend, the global opacity and the alpha handling are
already there, and a painter that sampled an image itself would be re-deciding all four.

- **Nearest-neighbour, stated rather than implied.** `sx = source.x + (x - dest.x) * source.width
  / dest.width` (and the same on `y`), in 64-bit arithmetic because two bounded numbers have an
  unbounded product. Exact at 1:1 — an unscaled draw *is* a copy — and deterministic at every
  other ratio. There is no filter policy in v1: no `image-rendering`, no interpolation quality,
  no mip levels.
- **`source` must lie inside the image** and have a non-zero area (or be empty, a successful
  no-op): a source rectangle that ran off the image is a caller reading pixels that do not exist,
  and it is refused rather than clamped, for the reason `GfxSurfaceCopyRect` refuses it —
  a clamped source silently draws a *different* picture.
- **`destination` is clipped, not refused**: the raster's current clip bounds what lands, exactly
  as it does for a fill, so a destination partly off the target draws the part that is on it.

## 6. Alpha

Alpha is preserved because the primitive goes through `GfxSurfaceGetPixel` (which converts
whatever the resource's format is into the logical RGBA8 this module composites in) and the
raster's per-pixel path:

- a source pixel with `a == 0` **leaves the target untouched** rather than filling it with
  transparent;
- `a == 255` writes the source's colour;
- anything in between blends source-over onto what was there, with the raster's **global opacity**
  multiplying the image's own alpha exactly as it multiplies a fill's — so a half-transparent
  image inside a half-opaque element composites to both numbers' product, and neither number is
  rewritten into the other.

`paint.image.alpha` pins all three on **one** source image over **one** opaque destination, so no
single answer can be explained by a pass that skipped the drawing.

## 7. A missing image is a number, not a crash and not a placeholder

There is no broken-image presentation in this runtime, and this phase does not invent one. An
image that is missing, pending, failed, refused, surface-less, zero-sized or absurdly large
produces **no record at all** — there is nothing to draw — and the traversal counts it:

| Counter | Meaning |
|---|---|
| `image_elements` | placed boxes whose element is an `<img>` — the traversal reaching an image *element* |
| `images` | `IMAGE` operations appended |
| `image_skipped` | image elements that produced no operation |

`image_elements = images + image_skipped` holds by construction, the same pair `text_runs = texts
+ text_skipped` forms one phase back — so "this document has no images" and "this document's
images drew nothing" are two different numbers rather than one silence.

The reasons a skip can happen, and where each is decided:

| Reason | Decided by |
|---|---|
| no `src` attribute, or an empty one | RoboHTML's resource state (`NONE`) |
| a `src` no resolver has run for | RoboHTML's resource state (`PENDING`) |
| a reference the resolver could not find | RoboHTML's resource state (`FAILED`) |
| a scheme this runtime will not fetch (`http:`, `https:`, `data:`, `file:`, …) | RoboHTML's resource state (`UNSUPPORTED`) |
| a resource with no pixel surface (`EMPTY`/`EXTERNAL`) | `PaintImageFromResource` → `RoboNotSupported` |
| a zero dimension, or above `RoboPaintImageMaxDimension` (16384) | `PaintImageFromResource` → `RoboNotSupported` |

**Geometry is deliberately not on that list.** A box with no area still produces an operation, and
the *drawing* pass is where a destination with no area is counted (`empty`) and nothing is drawn —
because "the document's resources are fine but there is nowhere to put the picture" and "the
document has no picture" are different facts and keeping them in different layers is what lets a
reader of either number know which they are looking at.

## 8. Geometry validation

Three layers validate, and each refuses what it can see:

- **the record** (`PaintImageIsValid`): a resource with a surface, positive intrinsic
  dimensions, and a source rectangle that is valid, positive and **inside** the resource (checked
  in 64 bits, because `x + width` of two bounded numbers is not bounded);
- **the command** (`PaintDisplayCommandIsValid`): the same payload rule plus a rectangle
  with a non-negative size and a valid clip;
- **the primitive** (`GfxRasterDrawImage`): the source inside the image, and the
  destination clipped to the raster's boundary.

The paint list's and display list's `append` doors run the record and command rules, so a list this
build produced cannot contain an image record that the pass would refuse. `paint.image.pixels`
asserts the pixel the destination rectangle claims is the pixel the source has; a wrong rectangle
fails there rather than in a picture somebody squints at.

## 9. Ownership, stated once

| Where | Holds | Released by |
|---|---|---|
| A payload a caller builds and hands to `PaintListAppend` | borrowed | the caller |
| An operation in a paint list | one reference | `PaintListClear`/`_destroy` |
| A command in a display list | one reference | `PaintDisplayListClear`/`_destroy` |

The builder copying a record's payload into a command therefore *adds* a reference rather than
moving one, and that is what makes the two lists independent: destroying the record list cannot
change a pixel the display list draws, and neither list depends on the element that named the
resource — releasing the document's own reference (`HtmlElementImageDetach`) leaves both
artefacts drawing exactly the same picture, while the resource itself stays alive until the last
of the three references is gone.

`paint.image.resource_lifetime` proves both halves: a cycle that detaches the document's reference,
destroys the record list and then paints the display list again is run **twice**, and the two
cycles must end with the same live-block count. The first cycle alone would prove the references
are real; the comparison is what proves they are given back.

## 10. Mutation isolation

An image payload is a copy of values plus a **reference** to a resource that is immutable by
construction (a decoded `GfxImage` is not written by anyone). So after a pass:

1. the element's `src` can be rewritten and re-resolved to a **different** 8x8 picture,
2. the DOM can be mutated freely,

and the record and the command still name the first resource, still say 4x4, and a display list
rebuilt from the same records is **equal** to the one built before the mutation
(`paint.image.mutation_isolation`).

## 11. Determinism

Two passes over one document produce equal record lists, equal display lists and equal pixels
(`paint.image.determinism`), and the dumps are byte-identical. The one thing in this module that
would break that is an address, which is why `identity` is stored and printed instead — the suite
asserts the dump contains **no `0x`**.

Two records built from the same resource compare equal; two records built from the *same bytes*
decoded twice do not. That is a statement about the resource rather than a defect in the
comparison: a document that resolves its images twice has two images.

## 12. The dump

```
PaintRecords:
#0003   begin-box      node=2 depth=1 clip=visible/visible border=0,0,200,100 ...
#0004   background     node=2 depth=1 ... color=#3366ccff
#0010   begin-box      node=3 depth=2 ...
#0011   image          node=3 depth=2 clip=visible/visible border=0,0,64,48 resource=7 src=0,0,4x4 source_size=4x4
#0012   content        node=3 depth=2 ...

DisplayList:
#0011   draw-image       node=3 depth=2 clip=visible/visible opacity=1.00 rect=0,0,64,48 resource=7 src=0,0,4x4 source_size=4x4
```

An image line prints the resource's **identity**, the source rectangle it samples and the
resource's own dimensions — and prints its colour **never**, because it has none: the colour is
printed for the kinds `uses_color` admits, which is the same predicate the validity rule reads. The
pixels are not printed for the reason a run's characters are not printed by the record's own writer:
a record *names* them.

## 13. Counters in the pass

`PaintDecorationStats` gains two terms and keeps its identities exact:

```
commands = draws + skipped
draws    = backgrounds + borders + texts + images
draws    = filled + contextual + transparent + empty + no_font + no_resource
```

`images` is a **subset of `filled`** rather than a term of the partition: an image that reached the
surface is a drawing that put pixels down, exactly as a fill is, and counting it in both lets a
reader ask either "how many of this document's drawings were images?" or "how many drawings reached
the surface?" without one answer being derived from the other. `no_resource` is the new partition
term: an image command whose payload this build cannot draw — unreachable through the public API,
and named anyway so a hand-built list is counted rather than silently swallowing a remainder.

## 14. What the numbers do not claim

- **No fetch, no decode, no network.** The resource arrives decoded. There is no URL, no loader, no
  cache, no `srcset`, no `<picture>` and no media query.
- **No intrinsic sizing for replaced elements.** RoboLayout does not yet size an `<img>` from its
  resource, so a document must give its images a CSS size (or a zero-sized box results, which is
  counted and drawn as nothing). This is the phase's largest limitation and it is **loadout's** to
  close, not a painter's.
- **No `object-fit`, no `object-position`, no nine-slice.** The mapping is "the full source into
  the destination rectangle layout produced", which is what `object-fit: fill` describes. A crop is
  a later addition *in shape already* — `source` exists on the payload and
  `PaintImageIsFullSource` is how a reader tells the two apart today.
- **No filters, no effects, no animation, no SVG.** An animated or vector resource is
  RoboGraphics' question, not this layer's.
- **No `border-radius`, `overflow`, `clip-path` or mask clipping of an image.** Images are clipped
  by the same rectangular boundary every other drawing is (11.8's scopes); rounded and shaped
  clipping remain future work.
- **No image-specific opacity architecture.** An image is drawn at the strength in force from
  11.9's scope exactly as a fill is; group opacity is still absent for the reason
  `Opacity.md` states.
- **No `image-rendering`, no interpolation quality, no mip levels.** Nearest-neighbour only.
- **Not a second graphics pipeline.** There is no Skia, no Blink image class, no Chromium
  dependency, and no image memory format of this module's own — the resource is RoboGraphics'.

## 15. Findings the phase recorded

1. **The colour rule had to be split, not extended.** `PaintOpKindDraws` was the predicate
   the validity rule was stated on. An image *draws* and has no colour, so the first attempt —
   "add `IMAGE` to `draws`" — would have refused every image in the document and made the phase
   look like it did nothing. The fix is `PaintOpKindUsesColor`, a positive list, and it is
   the shape that survives the next payload kind.
2. **`draws` is not enough for a *drawing*.** The validity rule's two halves were "a drawing
   carries a colour" and "a structural record carries `none`", and 11.7 adds a kind that is
   neither: it draws and its look is not a colour. So the rule is now three -- a colour kind
   carries a drawable colour, a structural record and an image carry `none`, and an image
   additionally carries a payload this build may draw -- and the predicate that decides which half
   a record is on is stated positively (`uses_color`) rather than as "everything that draws".
3. **A retained payload changes the containers.** Both lists were documented as arrays of values
   "with no pointers in them"; 11.7 makes a value name a resource, so `append` retains and
   `clear`/`destroy` release — one shared static walk per container, because a container that
   released on one path and not the other is a leak in a *failure* path.
4. **The lifetime test had to be a cycle, not a baseline.** A mem-debug baseline taken before a rig
   measures the harness as much as the module; running the whole cycle twice and comparing the two
   ends is what isolates the module's own ownership.
5. **Text's placement did not generalise.** A run is emitted a level *below* its box (it is a
   child object); an image is emitted *inside* its box (it is the element's content). The
   vocabulary's `IMAGE` row is in paint order while 11.9's two rows are not, and that asymmetry is
   now stated rather than assumed.
