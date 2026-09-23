/*
 * RoboPaint version pin — Phase 11.1 paint architecture, 11.2 paint records, 11.3
 * display lists, 11.4 background painting, 11.5 border painting, 11.6 text painting,
 * 11.7 image painting, 11.8 clipping, 11.9 opacity, 11.10 hit testing,
 * 11.11 paint invalidation, 11.12 rasterization (the bounded door and `raster`),
 * 12.16 the glyph-run bridge (shaped text on the record).
 *
 * **0.13.0 (12.16) is the release that moves text shaping out of the frame loop**, and it is the first
 * one whose subject is a *payload* rather than a drawing. Every release above this module's first one
 * answered a question about the sequence -- what will be drawn, in what order, bounded how, at what
 * strength -- and 12.16 answers one about a text run: **what is in it when it gets there**. The answer
 * changed twice before it was right:
 *
 *     11.6     the record carries the run's **characters**, and the painter looks each one up in a `cmap`
 *     12.4     the record carries the characters, and the painter resolves them through RoboText -- which
 *              shapes them **on every frame the list is replayed**
 *     12.16    the record carries the characters **and the glyphs**, and the painter shapes nothing
 *
 * The middle step was the architecturally right one and the operationally wrong one, and the reason is
 * worth stating because it is the entire phase: shaping is expensive (decode, select a face, segment,
 * GSUB, GPOS, kern, generate glyphs), it is **deterministic** given the same text and face, and a display
 * list is replayed many times. So it happens once, where the document is recorded, and what a replay walks
 * is eight numbers and a flag word per glyph (`PaintGlyph`).
 *
 * A glyph id alone is not enough, and that is the second half of the release: 42 is a letter in one face
 * and a different letter in another. So a run names its face by a **token** into a table the list owns
 * (`RoboPaintFontTable.h`) rather than by a borrowed pointer -- which keeps 11.6's property that a record
 * is a value with no pointers in it, a list is safe to copy and rebuild, and "no dangling pointer" is true
 * by construction rather than by discipline -- and a token that does not resolve is a **counted miss**
 * rather than a fallback, because a glyph id drawn with the wrong face is a different letter rather than a
 * wrong colour.
 *
 * The three consequences the release claims, each asserted where it is observable:
 *
 *   **a replay shapes nothing.** No decoding, no font selection, no GSUB, no GPOS, no kerning, no `cmap`
 *   lookup anywhere in the paint path -- and therefore no way for what is drawn to disagree with what was
 *   measured, because the numbers are the ones the measurement read;
 *   **a display list is a snapshot of shaped text**, at the same strength it already was for geometry: it
 *   owns the glyphs *and* the faces its tokens name, so it draws the same ink after the record list that
 *   produced it is gone -- and with **no face argument at all**, because the token resolves inside the list
 *   it came in;
 *   **the characters stay.** The record keeps both halves: the code points are what a selection, a hit test
 *   and a copy will read (Phase 12.21), and the glyphs are what a replay draws. A build that kept only the
 *   glyphs would be a list only one font could describe.
 *
 * What 12.16 does **not** do, each named where a reader would look for it: no glyph cache, no atlas, no
 * batching across compatible runs, no subpixel-phase-keyed rasterization and no multi-face recorder -- the
 * recorder is given one face (`PaintContextSetTextFace`) and the token vocabulary is what a later phase
 * lifts that with. It adds no `SAVE_STATE`/`RESTORE_STATE`, no layout, no line breaking, no caret or
 * selection geometry, no hit testing inside a run and no new drawing kind: the three drawing kinds and the
 * two scope pairs are exactly what 11.12 executed, with one of them carrying a different payload.
 *
 * **0.12.0 (11.12) is the release that closes this module's own capability table**, and it does it
 * with a change to *one door* rather than with a rasteriser of its own: the decoration pass gained
 * a **bound**. `PaintDecorationPaintBounded` executes the same list inside a rectangle the
 * caller names -- the same walk, the same scopes, the same strengths, the same drawings, with the
 * raster's clip narrowed by that rectangle -- and `PaintDecorationPaint` is now *that door
 * with no rectangle*, so the two are one code path and cannot disagree about a document.
 *
 * The capability this release flips is `raster`, and the honest reading of the flip matters more
 * than the flip: **RoboPaint still has no rasteriser, and it should not have one.** What the field
 * has always asked -- "is there a subsystem that executes a whole display list into pixels?" -- is
 * now answered by the module directly above this one, `RoboRender` (11.12), whose version header
 * and `render/docs/Rasterization.md` argue the arrangement: this module owns *what the list means*
 * (the command vocabulary, the two scope stacks, the per-drawing boundary and strength) and that
 * module owns *what a frame is* (an owned target, a run, an execution record, byte-determinism,
 * region execution). A caller asking whether a document can be turned into pixels is asking about
 * the pair, and a capability table that left this field at 0 while `RoboRender` existed would be
 * the one lie in a table whose whole purpose is to be readable.
 *
 * What 11.12 does **not** do, and each is named where a reader would look for it: it does not add
 * a tile tree, a damage history, frame scheduling, a compositor or a GPU backend; it does not
 * make a partial repaint cheaper *as a whole frame* (a region run walks the whole list -- it is the
 * pixels that are bounded, not the walk); it does not add `SAVE_STATE`/`RESTORE_STATE` (11.3's
 * reasoning stands: there is no graphics state to save because every drawing writes the boundary
 * and the strength it is drawn under); and it does not present anything to a window or a
 * framebuffer -- 11.13 added that, one module up, and it did it without a door here: `RoboRender`
 * gained `RenderPresent`, and `RoboDesktop` is the shell that decides what is on a screen.
 *
 * **0.11.0 (11.11) is the release that answers a question the ten before it could not**: given a
 * picture this module already produced, which of its pixels are no longer the pixels the document
 * describes? The answer is a **set of regions** (`RoboPaintInvalidation.h`), each with a reason,
 * the boundary it was drawn under and a flag saying whether it is a conservative whole-extent
 * answer -- filled by explicit marks, by comparing two artifacts, or by reading the
 * `styleOnly` count 11.1 left for it. The version moved and `paintInvalidation` flipped from the
 * 0 it had been since the first release; `raster` was then the **only** field still 0, and that was
 * the point of the pair, because "which regions changed" and "execute the whole list" are
 * different capabilities and a caller needs both answers. **11.12 answered the second one**, in the
 * module above this one (`RoboRender`), so `raster` is 1 as of 0.12.0 -- see that release's note
 * above and `RoboPaint.h`'s field comment for the width of the claim.
 *
 * 0.1.0 was the first release of the fifth module in the layout stack and the first
 * one whose subject is not a *conclusion* but a **sequence of work**: RoboLayout
 * answers where every box is, and RoboPaint turns those answers into an ordered list
 * of operations a rasteriser will one day execute. `DEPENDENCIES.md` puts it directly
 * above RoboLayout and directly below RoboRender, and this release is the boundary
 * between them and nothing above it.
 *
 * The one idea this phase turns on, and getting it backwards would produce a module
 * that is fast and wrong: **a paint list is a record of what will be drawn, not a set
 * of drawing instructions, and it is produced by one deterministic traversal of the
 * layout tree.** Three consequences follow, and each of them is a decision rather
 * than a detail:
 *
 *   the **order** comes from the tree, not from the list. A flat, contiguous sequence
 *   with an explicit BEGIN/END pair per box is what a rasteriser consumes, because it
 *   walks once and keeps a clip stack -- so the shape of the tree is *recoverable*
 *   from the list by the depth each operation carries, and a nested structure would
 *   only have to be flattened before use;
 *
 *   an operation is emitted **only when it draws**. A box with no background
 *   declaration emits no background operation and a side whose used width is zero
 *   emits no border operation, so the list's length is a statement about the document
 *   rather than about the traversal. This is the rule that keeps "no speculative
 *   operation" true while the operation vocabulary stays small;
 *
 *   paint **derives nothing RoboLayout already concluded**. The border box, the two
 *   insets, the clip rectangle and the four used border widths all come from layout's
 *   own doors; the only arithmetic this module performs is the two things layout
 *   cannot know -- the accumulation that turns per-containing-block coordinates into
 *   one page space, and the intersection that turns a nested boundary into one clip
 *   rectangle.
 *
 * What this build does **not** do is draw. `RoboPaintDrawingImplemented` is 0 and
 * `PaintDrawingIsImplemented()` answers 0 with it, so a caller cannot mistake
 * "the pass ran" for "the pixels are there" -- the same three-place statement
 * RoboLayout 10.1 made about geometry, for the same reason. What exists is the
 * context, the operation vocabulary, the list and its ownership, the deterministic
 * traversal, the geometry a rasteriser will read, and the clip state. Rasterisation,
 * display lists, text, images, opacity and paint invalidation are each a named later
 * goal, and every one of them is named in `paint/README.md` where it would otherwise
 * be assumed.
 *
 * 11.2 adds one enumerator and no new door, and the honest way to say what it is is
 * that the record model was already right: an operation was a value snapshot with no
 * pointers, the list owned it, the traversal ordered it deterministically and the dump
 * read it back, so "records" is what the phase *consolidates* rather than something it
 * introduces. What it adds is the record the first release's sequence was missing --
 * `CONTENT`, the boundary between a box's own records and its descendants' -- and the
 * rule that makes the sequence regular: **structural records are unconditional, drawing
 * records are conditional**. A consumer can now read a box's own painting as
 * `[BEGIN_BOX .. CONTENT]` and its subtree as `(CONTENT .. END_BOX)` without inferring
 * the boundary from the depth of the next child's marker, and a childless box has a
 * boundary exactly as a parent does. Nothing else about a record changed: same fields,
 * same ownership, same two coordinate spaces, same clip. `RoboPaintDrawingImplemented`
 * is still 0, because a `CONTENT` record is a statement about the sequence and not a
 * pixel either.
 *
 * 11.3 is the first release *above* the record list: the **display list**. A paint
 * operation says which rectangles a decision was made from -- the box model, plus a kind
 * -- and leaves one derivation to the consumer, the band a border side occupies. A
 * display command says which **one rectangle to act on**, resolved, and makes the
 * traversal's structural convention an invariant it enforces. Two things are worth
 * stating so that neither is mistaken for duplication: the padding box *disappears*, and
 * the disappearance is the point (it was an input to the band derivation, and nothing
 * above needs it once the band is resolved); and the four border kinds *collapse into
 * one*, because the fact that distinguished them -- which side -- is consumed into the
 * band rectangle. A display list is therefore **render-ready**: a rasteriser walks it
 * once and never consults the box model. `RoboPaintDrawingImplemented` was *still* 0 at
 * 11.3: a display list is data, and nothing there put a colour on a surface.
 *
 * 11.4 is where the colour finally reached one. `PaintDecorationPaint` walks a
 * display list and fills each **drawing** command into a RoboGraphics surface, so
 * `RoboPaintDrawingImplemented` is **1** -- the condition this header wrote down for the
 * flip was exactly "a build in which an operation's rectangle and colour reach a RoboGraphics
 * surface", and that is now true for a box's background and, since 11.5, for each of its
 * border sides. The fence that keeps the claim honest is `raster` beside it, which stays **0**:
 * the three **structural** command kinds are not drawings and are skipped (and counted), so
 * what is filled is the drawing kinds of the list and not the list. The colour crosses into
 * RoboGraphics through one door (`PaintColorToRgba8`), which is a field-for-field copy
 * between two 8-bit sRGB records rather than a conversion -- and `currentColor` is *refused*
 * there, because only the layer that owns an element's `color` can resolve it, and the paint
 * pass has already done so before emitting.
 *
 * The colour resolution is the one place this release reads a property RoboLayout
 * deliberately ignored, and it is worth stating because it looks like a violation of
 * the boundary: 10.14 classifies a property name into "layout does not read it",
 * "paint-visible only" or "feeds geometry", and `background-color` and
 * `border-*-color` are the * * paint-visible** half. So a colour is exactly what this
 * module is *supposed* to read, and `currentColor` -- which `RoboLayoutBorder.h` says
 * "only the layer with an element can resolve" -- is resolved here, because this is
 * that layer.
 *
 * 11.5 is the second drawing and the first one with a **shape**, and the two halves of it are
 * worth stating because only one of them is about a rectangle reaching a surface. The border
 * **style** had been dropped one stage too early: 11.3 resolved the band a side occupies and
 * collapsed the four side kinds into one command, and in doing so discarded the fact that a
 * `dashed` band is not a `solid` one -- which is exactly the fact a rasteriser cannot
 * reconstruct, because it is not derivable from the band's rectangle. So the style now travels
 * record → command, and a command is render-ready in the sense this header keeps using it: its
 * consumer can produce the drawn shape from the command alone. What this build draws for the
 * seven *pattern* styles is their **band**, in the side's colour -- CSS's `solid` shape -- and
 * that simplification is a **number** (`stats.unshaped`) rather than a silence, because a
 * border drawn as a solid band when the author asked for dots is exactly the quiet wrongness
 * a rendering module should be counting instead of shipping.
 *
 * The one architectural decision 11.5 had to make, and the reason there is one painter rather
 * than a second: **backgrounds and borders are one pass, because they are one order.** The
 * list interleaves them the way the traversal emits them, and CSS's paint order is that same
 * order -- a descendant's background is painted *after* its ancestor's border, so a child that
 * spills over its parent draws over the parent's border rather than under it. Two passes would
 * paint every background in the document before every border in it, which is a different and
 * visibly wrong picture. So the background-only door of 11.4 was **replaced**, not joined, by
 * `PaintDecorationPaint` -- the fence it drew (`backgroundPainting` **1**, `raster`
 * **0**) is the same fence, moved one command kind along.
 *
 * **11.6 -- text painting**, and the first thing this module carries that is not a number.
 * A text node generates a layout object of kind `TEXT` and nothing painted it, because a run's
 * rectangle is a line fragment with no box layers and drawing one means glyphs. It now draws:
 * a `TEXT` record and a `draw-text` command carry the run's **characters** in a pool the list
 * owns, the **baseline** as an offset from the run rectangle's top, and the **font inputs** the
 * run was measured with, and the same pass that fills backgrounds and bands resolves each
 * character through a face's cmap and strokes its outline.
 *
 * Three decisions are worth the space, because each of them was a choice between two
 * defensible designs and the reason to pick one is not obvious:
 *
 *   **characters, not glyph ids.** A glyph id is a fact about a *font*, so a display list that
 *   stored them would be a list only that font could draw, and the face the traversal resolved
 *   glyphs with and the face the painter drew with would be two faces nothing checks for
 *   agreement. Storing code points keeps the display list a statement about the **document** --
 *   independent of the font it will be drawn with, exactly as a background command is independent
 *   of the surface it will be filled into -- and puts glyph resolution where RoboGraphics' own
 *   bridge says it belongs: *"RoboGraphics does not shape, segment, bidi, or fall back fonts.
 *   RoboText supplies glyph IDs + placement later."*
 *
 *   **a pool and a span, not a pointer.** Characters are variable-length data and no fixed
 *   record can hold them, so the record carries two numbers that name them and the list owns
 *   the array. That is what keeps "a value record with no pointers in it" true -- and with it
 *   every property 11.1 built on that: a list is safe to copy, to discard and to regenerate, and
 *   a text operation can no more dangle than a fill can.
 *
 *   **the font is the caller's, exactly as the surface is.** `PaintDecorationPaint`
 *   takes a face, borrows it, retains nothing, resolves no `font-family` and falls nothing back;
 *   NULL is legal and counts every text command (`noFont`) rather than failing. It is the same
 *   argument `RoboLayoutTextFont.h` makes for its measurer: the face is handed in, and which
 *   face a document's text should use is a selection question whose owners are elsewhere.
 *
 * What the release does **not** claim: shaping (no GSUB, no GPOS, no ligatures, no kerning), the
 * bidi algorithm (an RTL run is drawn left to right, counted as `rtl`), line breaking,
 * whitespace collapsing, font fallback, `text-decoration`, `text-shadow`, and filled or
 * antialiased glyphs -- this build strokes glyph outlines, because that is the only glyph mode
 * `RoboGraphicsText.h` implements (`MASK`, `ALPHA` and `COLOR` are reserved there and refused).
 * `textPainting` says a run's glyphs reach a surface; it does not say they look the way a text
 * renderer's output looks. Shaping is Phase 14's, and the boundary is already the right shape:
 * this build resolves each code point through the cmap in logical order, and Phase 14 replaces
 * that step with a glyph run and changes nothing above it.
 *
 * **11.8 -- clipping**, and the first release of this module that is about *where a drawing
 * stops* rather than what it puts down. The record already carried the boundary in force on
 * every command, and the drawing pass already bounded each command by it, so the pixels were
 * clipped before this phase began; what was missing was the **scope**. A flat list said
 * "this command is bounded by `hidden[0,200]/visible`" on every descendant and nowhere said
 * where one boundary began and another ended -- so a clipping rectangle could not be checked
 * against anything, a consumer could not know what to restore after a nested box, and
 * `overflow` was a field rather than a structure.
 *
 * Three decisions, and each is the kind that looks like an implementation detail until a
 * later phase depends on it:
 *
 *   **a scope is a pair of commands, not a state flag.** `PUSH_CLIP` carries the effective clip
 *   and stands after the box's `CONTENT`; `POP_CLIP` carries the same record and stands after
 *   the box's last child. What is in force at any point is the innermost open push -- so a
 *   consumer replaying the list needs a stack and no geometry, and the extent of a boundary is
 *   recoverable from the sequence rather than from the tree;
 *
 *   **the scope is checked, not assumed.** The builder refuses a pop with no push, a pop whose
 *   boundary is not the one its push carried, a scope opened twice by one box, a scope wider
 *   than the boundary it is inside, a scope that establishes nothing, and any command inside a
 *   scope carrying a different boundary. That is what makes the pair load-bearing: without the
 *   last rule a push and a pop would be two commands a consumer could only skip, and the
 *   rectangle a drawing is bounded by would still be a field nothing cross-checks;
 *
 *   **the boundary clips the contents, never the box.** A push stands *after* the box's own
 *   background and border commands and before its children, because CSS clips an overflowing
 *   *child* at the ancestor's padding edge -- the ancestor's own decoration is painted in the
 *   scope the ancestor was entered under. Getting this backwards would clip a `overflow: hidden`
 *   box's own border away, which is a rendering bug no interval assertion would catch.
 *
 * The clip **record** did not change: still two axes with an interval each, because a single
 * rectangle cannot express `overflow-x: hidden` beside `overflow-y: visible` and a consumer
 * handed four numbers would clip an axis the author left open. What is new beside the
 * commands is the **stack** (`RoboPaintClip.h`), the clip-to-clip intersection
 * (`PaintClipIntersectClip`), and the empty-scope predicate
 * (`PaintClipIsEmpty`), which is the answer to "is there any point in drawing this?"
 * for a box an ancestor's boundary already excluded.
 *
 * What the release does **not** claim: rounded corners (`border-radius` clipping), arbitrary
 * clip paths, `clip-path`, masks, filters, scroll containers and their clipping, tile clipping,
 * compositor-level clipping, or `overflow: auto`/`scroll` -- an `auto` or `scroll` axis resolves,
 * in this build, exactly as `hidden` does, because scrolling is a later goal's and the *layout*
 * answer for such an axis is already the one `RoboLayoutOverflow.h` gives. Nor does it claim
 * `SAVE_STATE`/`RESTORE_STATE`: the raster rebuilds its clip from the command it is about to draw,
 * so there is no graphics state for a pair like that to save, and the two commands would be ones
 * a consumer could only skip. A later pass with real graphics state adds them as one enumerator
 * and one row.
 *
 * **11.9 — opacity.** The first release whose subject is not *what* is drawn or *where it
 * stops*, but **how strongly it lands**, and the second state this module carries that is not a
 * colour. It arrived with an unusual amount of the work already done and in the right shape: an
 * element's `opacity` was a computed value RoboCSS already represented (11.9 added the property
 * and its `OPACITY` computed-value kind, because a build that read a *number* out of whatever
 * value it found would read a `200px` width as an opacity of `200`), and RoboGraphics already
 * had a raster-wide global opacity that multiplies a source alpha before the blend. So the
 * phase is almost entirely about the **path between them** -- and about one decision that had
 * to be made before any of it could be written.
 *
 * That decision is what `opacity` means, and getting it wrong produces a build that looks right
 * on every single-element test and is wrong on every nested one. **Opacity does not inherit**
 * (`RoboCSSPropertyRegistry.c` registers it non-inherited and argues why): inheritance copies a
 * value down to a child that declared nothing, so an inherited opacity would let a child at
 * `opacity: 1` inside a parent at `0.5` become fully opaque again. What actually happens is that
 * the parent's opacity applies to **everything the parent contains** -- so the child is drawn at
 * `0.5`, and a grandchild at `0.25`. That is an accumulation along a **path**, which is why it
 * lives in the traversal and in a record (`RoboPaintOpacity.h`) rather than in the cascade, and
 * why every operation and every command carries the **effective** strength rather than the
 * declared one.
 *
 * The record is the clip record's shape and the scope is the clip scope's shape, and the *one*
 * structural difference between the two pairs is worth stating because it looks like an
 * inconsistency and is the opposite: **a clip belongs to a box's contents, an opacity belongs to
 * the box itself.** CSS paints a `0.5` box's own `background-color` at half strength and paints
 * that same box's background under its *parent's* clip -- so the opacity pair opens at the box's
 * marker and before its first decoration, while the clip pair opens at its content boundary and
 * after them. Both are the same rule read at two points: every record carries the state in force
 * at its own position in the sequence, and both halves of a pair carry the value the pair
 * establishes, which is what makes a pair checkable from the stream.
 *
 * Three further decisions, each of which had a plausible alternative:
 *
 *   **a real value, not a "zero means unset" literal.** `PaintOpacityInit` clears to
 *   *fully opaque*, so a record this module never asked about draws at full strength -- the
 *   opposite direction from a clip record's cleared state, and stated as a door for exactly
 *   that reason. A cleared record that meant "invisible" would make every element of every
 *   document that never mentions `opacity` disappear;
 *
 *   **per drawing, not per subtree.** CSS composites an element's whole subtree as a group and
 *   applies the opacity to the flattened result; this build multiplies per drawing, which is
 *   exact for a subtree whose contents do not overlap and differs where they do (two overlapping
 *   children of a `0.5` parent composite as `0.5*(a over b)` under CSS and as
 *   `(0.5*a) over (0.5*b)` here). Group opacity needs an offscreen layer to composite into, which
 *   is a compositor and a later phase's, so this build carries the multiplication and **says so**
 *   -- the record's shape is what a group implementation will replace, a flag and a layer id, and
 *   nothing above it has to change;
 *
 *   **alpha stays alpha.** A colour's own alpha and an element's opacity are two numbers and
 *   neither is rewritten into the other: `final_alpha = colour_alpha * effective_opacity` is the
 *   raster's arithmetic (`PaintOpacityToByte` crosses the `[0, 1]`-to-`[0, 255]` gap once,
 *   in the paint module, and the raster multiplies), so a half-transparent fill inside a half
 *   opaque element composites the way both numbers say it should.
 *
 * What the release does **not** claim: group compositing (below), `mix-blend-mode`, filters,
 * `backdrop-filter`, masks, `transform`, `transition`, `animation`, a compositor or its layers,
 * or opacity on anything but the drawings this build has -- images, gradients and shadows are
 * still absent, so a faded gradient is not a thing this build can be asked for yet.
 *
 * **11.10 -- hit testing**, and the first release of this module whose subject is not what is
 * drawn at all. Every release above answers a question about the **sequence**: what will be
 * drawn, in what order, bounded how, at what strength. This one answers the **inverse** -- given
 * one point on the drawing plane, which node would be drawn there -- and the interesting thing
 * about it is how little new machinery it needed. The geometry is RoboLayout's, the boundary in
 * force is 11.8's record and the strength in force is 11.9's, so the phase is a *reader* of
 * three conclusions rather than a fourth model of any of them.
 *
 * The decision that shapes it is where it lives. A new top-level subsystem is the natural
 * reading of the phase and the wrong one: a module beside RoboPaint would have to either
 * duplicate `PaintClip` and `PaintOpacity` or depend on paint for two types and
 * reach past it for the geometry -- two boundaries where there was one -- and what a hit test
 * actually shares with the painter is deeper than the two records. It shares **the walk**: both
 * accumulate a page origin, narrow a boundary and multiply a strength down the tree. So this is
 * a subsystem *inside* RoboPaint with its own installed header, source, suite and family
 * document, and **no new dependency edge** -- `RoboPaintHitTest.h` argues it at length.
 *
 * Four rules decide the answer, and each mirrors the painter exactly so that the two cannot
 * disagree about a document:
 *
 *   **last drawn wins.** A box's own decorations are painted before its subtree and its children
 *   are painted first-to-last, so the walk examines a box's children **before** the box and the
 *   **last** child before the first. A document-order walk answers with the node *underneath*
 *   the one on top, which is the classic hit-test bug and the reason the order is stated
 *   first;
 *
 *   **a box's region is its border box.** A background fills it (11.4), a border band is inside
 *   it and a content box is inside it, so the three layers' union is the border box for every
 *   kind of box this build has -- and a box that draws *nothing* is still an answer, because CSS
 *   hits a transparent `div` over its whole border box. Painting nothing is not the same as
 *   being absent, and the alternative would need `pointer-events` machinery this build has
 *   none of;
 *
 *   **the boundary clips the contents, never the box** -- 11.8's arrangement read backwards, and
 *   the direction a naive hit test gets wrong invisibly. A child outside an `overflow: hidden`
 *   ancestor's boundary must not be hit while that **ancestor's own border band** must still be,
 *   so the walk enters a box under the boundary in force and descends into its children under
 *   the boundary that box's own `overflow` puts in force;
 *
 *   **opacity does not affect hit testing, `opacity: 0` included.** CSS governs hit testing with
 *   `pointer-events`, not with `opacity` -- a fully transparent element still receives a point,
 *   because an author who wanted otherwise would have written `pointer-events: none` or
 *   `display: none`. This build has no `pointer-events`, so the property's initial value applies
 *   and the strength travels in the **result**, where a later phase implementing the property
 *   finds the number already.
 *
 * Three further decisions worth stating, because each had a plausible alternative:
 *
 *   **the region 10.13 measured has a consumer at last.** A point outside a box's **region** --
 *   its border box grown by the extents its contents reached past it -- cannot be on any node
 *   beneath it, so the walk tests that one rectangle before descending. That is what makes a
 *   query proportional to the boxes the point is near rather than to the document, and it is why
 *   `rejectedRegion` exists beside `rejectedClip`: the two reasons a subtree was skipped are
 *   different questions with different answers;
 *
 *   **a stale tree is refused, and the refusal is an outcome rather than an error.** A hit test
 *   against half-computed geometry is not a *less accurate* answer, it is a **wrong** one: it
 *   names whichever node holds a stale rectangle, and a caller about to dispatch a click on that
 *   answer would deliver it to a node the user is not pointing at. So `RoboPaintHitStale`
 *   carries no node, exactly as `RoboPaintPassStale` carries no list;
 *
 *   **the answer is a snapshot.** The result copies the identity, both coordinate spaces, the
 *   geometry and the two states in force rather than referencing the tree, so a style change
 *   after a query cannot rewrite a conclusion already drawn -- the rule 11.9's records follow,
 *   for the same reason.
 *
 * What the release does **not** claim: **event dispatch** (the result names one node, and walking
 * up to the nearest element that could listen is Phase 12's), event bubbling, `pointer-events`,
 * caret or character mapping inside a text run, text selection, `visibility` (the property is
 * registered and consumed by *nobody* in this runtime, so reading it here would make a hit test
 * refuse a point on something visibly drawn -- the limitation is stated rather than papered
 * over), `border-radius` or `clip-path`-shaped hit regions, masks, filters, transforms, alpha or
 * SVG geometry, scroll offsets, zoom, device-pixel-ratio scaling, `:hover`-style state, focus
 * management, or a compositor-level hit test. Hit testing is over the rectangles the painter
 * draws, and every one of those needs a shape or a space this build does not have.
 */
#ifndef ROBO_PAINT_VERSION_H
#define ROBO_PAINT_VERSION_H

#define RoboPaintVersionMajor 0
#define RoboPaintVersionMinor 13
#define RoboPaintVersionPatch 0
#define RoboPaintVersionString "0.13.0"

/** 1 once the Phase 11 surface is frozen (not yet — see the header comment). */
#define RoboPaintApiFrozen 0

/**
 * 1 since 11.4: this build puts a colour on a surface.
 *
 * The flip happened exactly where this header said it would -- "a build in which an
 * operation's rectangle and colour reach a RoboGraphics surface" -- and the things that
 * reached it are a box's **background** and, since 11.5, its **border bands**:
 * `PaintDecorationPaint` fills a display list's drawing commands through the
 * RoboGraphics CPU raster.
 *
 * It is read by two doors that must not disagree -- `PaintGetCapabilities()` with
 * its `drawing` field, and `PaintDrawingIsImplemented()` -- and by the conformance
 * suite, which asserts all three together. A caller deciding whether to look for a surface
 * should read the function; a caller deciding whether to *make* one should read the field.
 *
 * What it claims is narrow and it must not be read wider: a **background**, a **border band**
 * and, since 11.6, a **text run's glyphs**, in the colours CSS resolved and, since 11.8, bounded
 * by the clip **scopes** the document's `overflow` puts in force. It does **not** claim
 * the seven pattern border styles (the band is drawn, the pattern is not -- `unshaped` counts
 * them), glyphs drawn as filled or antialiased shapes rather than stroked outlines, shaping or
 * bidi (`rtl` counts what this build drew in logical order), rounded corners, gradients,
 * transforms or a compositor, and it does **not** claim a rasteriser of the whole list -- that is
 * `raster`, which is still 0, because the structural commands are not drawings. Since **11.9** it
 * does claim an element's `opacity`, applied on top of the colour's own alpha per drawing (see
 * `opacity` in `RoboPaint.h` and the group-opacity deviation `RoboPaintOpacity.h` states). A caller that read this constant as "the document
 * renders" would be wrong in exactly the way reading `RoboLayoutGeometryImplemented` as
 * "layout is done" was wrong in a first release; the fields beside it -- `backgroundPainting`,
 * `borderPainting`, `textPainting`, `imagePainting` and `clipping` -- say which drawings exist
 * and what bounds them. **Since 11.7 an `imagePainting` beside them also claims a document's
 * images**, drawn from a resource the document already resolved: no fetch, no decode, no intrinsic
 * sizing and no `object-fit` (`RoboPaint.h` states the width of that claim in full), with the
 * scaling and the alpha blending being `GfxRasterDrawImage`'s.
 */
#define RoboPaintDrawingImplemented 1

#endif /* ROBO_PAINT_VERSION_H */
