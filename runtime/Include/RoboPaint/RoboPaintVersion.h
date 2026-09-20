/*
 * RoboPaint version pin — Phase 11.1 paint architecture, 11.2 paint records, 11.3
 * display lists.
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
 * What this build does **not** do is draw. `ROBO_PAINT_DRAWING_IMPLEMENTED` is 0 and
 * `robo_paint_drawing_is_implemented()` answers 0 with it, so a caller cannot mistake
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
 * same ownership, same two coordinate spaces, same clip. `ROBO_PAINT_DRAWING_IMPLEMENTED`
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
 * once and never consults the box model. `ROBO_PAINT_DRAWING_IMPLEMENTED` is *still* 0:
 * a display list is data, and nothing here puts a colour on a surface.
 *
 * The colour resolution is the one place this release reads a property RoboLayout
 * deliberately ignored, and it is worth stating because it looks like a violation of
 * the boundary: 10.14 classifies a property name into "layout does not read it",
 * "paint-visible only" or "feeds geometry", and `background-color` and
 * `border-*-color` are the **paint-visible** half. So a colour is exactly what this
 * module is *supposed* to read, and `currentColor` -- which `RoboLayoutBorder.h` says
 * "only the layer with an element can resolve" -- is resolved here, because this is
 * that layer.
 */
#ifndef ROBO_PAINT_VERSION_H
#define ROBO_PAINT_VERSION_H

#define ROBO_PAINT_VERSION_MAJOR 0
#define ROBO_PAINT_VERSION_MINOR 3
#define ROBO_PAINT_VERSION_PATCH 0
#define ROBO_PAINT_VERSION_STRING "0.3.0"

/** 1 once the Phase 11 surface is frozen (not yet — see the header comment). */
#define ROBO_PAINT_API_FROZEN 0

/**
 * 0 until this build puts a colour on a surface.
 *
 * A constant that is 0, which is the point: RoboLayout 10.1 shipped
 * `ROBO_LAYOUT_GEOMETRY_IMPLEMENTED` as 0 for the same reason, because the honest
 * thing a first architecture release can say about the feature it is *for* is that
 * the feature is not there yet.
 *
 * It is read by two doors that must not disagree -- `robo_paint_get_capabilities()`
 * with its `drawing` field, and `robo_paint_drawing_is_implemented()` -- and by the
 * conformance suite, which asserts all three together. A caller deciding whether to
 * look for a surface should read the function; a caller deciding whether to *make* one
 * should read the field.
 *
 * What it will claim when it flips, stated now so that the flip is a decision rather
 * than a discovery: a build in which an operation's rectangle and colour reach a
 * RoboGraphics surface. It will **not** claim antialiasing, rounded corners, gradients,
 * opacity beyond the colour's own alpha, or a compositor. Those are later goals', and
 * a phase that flipped this constant while adding one of them would be claiming more
 * than it did.
 */
#define ROBO_PAINT_DRAWING_IMPLEMENTED 0

#endif /* ROBO_PAINT_VERSION_H */
