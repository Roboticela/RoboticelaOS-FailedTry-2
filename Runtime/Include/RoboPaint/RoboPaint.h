/*
 * RoboPaint — RoboticelaOS Runtime's paint architecture (Phase 11.1–11.8).
 *
 * Dependency:
 *   RoboBase / RoboMem → RoboGraphics → ... → RoboCSS / RoboHTML → RoboLayout
 *       → RoboPaint → RoboRender → RoboCompositor
 *
 * RoboPaint sits directly above RoboLayout and directly below RoboRender
 * (DEPENDENCIES.md): it may read a layout tree, a computed style's paint-visible
 * properties, RoboGraphics' geometry and -- since 11.4, where a background reaches a
 * surface -- RoboGraphics' CPU raster primitive, and it must not depend on render,
 * compositing, the shell, the API layer, JS, a window or a display. The boundary scan
 * (`Tools/Build.ps1 -Verify`) enforces it, and the direction that would be invisible to
 * a layering argument -- paint reaching *down* for a window to clip to -- is the one the
 * scan is really for.
 *
 * Phase 11.1 is architecture: the paint context, the operation vocabulary, the list and
 * its ownership, the geometry boundary, the clip state, the deterministic traversal and
 * the dump. And since 11.4 it paints: `PaintDecorationPaint` walks a display list and
 * fills every **drawing** command into a caller's RoboGraphics surface, so
 * `PaintDrawingIsImplemented()` answers 1 -- a box's **background** (11.4) and, since
 * 11.5, each of its **border sides** -- and since 11.12 `raster` is 1 as well, because the
 * subsystem that executes a whole display list into pixels now exists directly above this module
 * (`RoboRender`): this module owns what the list *means* -- the walk, the two scope stacks, the
 * per-drawing boundary and strength -- and that one owns what a *frame* is. What 11.12 added here
 * is the **bounded** door (`PaintDecorationPaintBounded`), which is how a region run
 * executes the same list inside a rectangle without a second pass existing to disagree with it.
 *
 * The pipeline this module is the fourth stage of, and where this release stops:
 *
 *   HTML / DOM → RoboCSS → RoboLayout → **RoboPaint** → display list →
 *   **background and border fill** → RoboGraphics → RoboWindow / display
 *
 * What this release buys the phases that follow: a tree that has been laid out can be
 * turned into an ordered list of operations, every operation carries the box it draws
 * and the two coordinate spaces it lives in, and the list is the *only* thing that has
 * to grow when a later goal adds text, images, opacity, transforms or clipping -- the
 * record already has a node, a depth, three rectangles, a clip and a colour, and a new
 * kind is one enumerator rather than a redesign.
 *
 * What it does *not* buy, at any version, is a **rasteriser of its own**, and that is a boundary
 * rather than a gap: a pass that consumed a whole list *and* owned an execution record, a target
 * and its byte-determinism would be two modules' jobs in one, and a renderer built on it could
 * not be tested without a document. What the drawing doors buy is narrower and precise -- the
 * *drawing* command kinds reach a surface, bounded by the scopes the document declares and at the
 * strengths it declares -- and a caller that checked only a version number would conclude that
 * 0.5.0 renders a document, when what it renders is a box's background and its four border bands
 * (as bands: seven of CSS's ten border styles are pattern-shaped inside the band and are counted
 * rather than shaped, see `RoboPaintDecoration.h`). Since 11.12 the claim `raster` makes is
 * narrower than it reads, and `render/docs/Rasterization.md` says exactly how wide it is: a whole
 * list is *executed* -- state scopes honoured, every drawing attempted -- while a `rasteriser`'s
 * harder promises (a tile tree, damage tracking, a frame scheduler, a compositor, a GPU) are each
 * named as absent rather than implied.
 *
 * Chromium's paint and display-list architecture -- `PaintLayer`, `DisplayItem`,
 * `PaintController`, `cc::DisplayItemList` -- is temporary technical reference only, and
 * nothing here is a translation of any of it. The design decisions that matter are made
 * the other way round from Chromium's, and the two worth naming are that this list
 * carries **no transform stack and no property tree** (there is nothing to inherit yet) and
 * that it carries **both coordinate spaces per operation** rather than one plus a walk. See paint/README.md and paint/docs/.
 */
#ifndef ROBO_PAINT_H
#define ROBO_PAINT_H

#include "RoboBase/RoboError.h"

#include "RoboPaint/RoboPaintClip.h"
#include "RoboPaint/RoboPaintDisplayCommand.h"
#include "RoboPaint/RoboPaintDisplayDump.h"
#include "RoboPaint/RoboPaintDisplayList.h"
#include "RoboPaint/RoboPaintDisplayListBuilder.h"
#include "RoboPaint/RoboPaintColor.h"
#include "RoboPaint/RoboPaintContext.h"
#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintDump.h"
#include "RoboPaint/RoboPaintFontTable.h"
#include "RoboPaint/RoboPaintGeometry.h"
#include "RoboPaint/RoboPaintHitTest.h"
#include "RoboPaint/RoboPaintList.h"
#include "RoboPaint/RoboPaintOpacity.h"
#include "RoboPaint/RoboPaintOperation.h"
#include "RoboPaint/RoboPaintRun.h"
#include "RoboPaint/RoboPaintText.h"
#include "RoboPaint/RoboPaintTypes.h"
#include "RoboPaint/RoboPaintVersion.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Module lifecycle                                                    */
/* ------------------------------------------------------------------ */

/**
 * What this build of RoboPaint actually does.
 *
 * Capabilities, not marketing: the module owns no global state to initialise, so its
 * capabilities are a statement about which *features* exist -- and four of them are
 * deliberately 0, because the point of an architecture release is that the road ahead is
 * named rather than implied.
 *
 * The fields are `int` in declaration order with no padding, which
 * `paint.conformance.capabilities` pins by walking the struct positionally against a
 * list of names -- so a field added here without an assertion is a visible omission
 * rather than a silently untested flag. That is RoboLayout's device, and it is worth
 * keeping for the same reason: `capabilities` is a struct, and a struct read
 * positionally is the one way to assert it without writing an accessor per field.
 */
typedef struct PaintCapabilities {
    /** The paint context and its pass record (11.1). */
    int context;
    /** The operation vocabulary and its validity rules (11.1). */
    int operationModel;
    /** The operation list, its growth, its read doors and its ownership (11.1). */
    int paintList;
    /** The deterministic traversal that produces operations from a layout tree
     *  (11.1). */
    int traversal;
    /** Reading a box's border, padding and content boxes out of layout's result and
     *  its two inset records (11.1). A claim about the *build*: a box nothing placed
     *  has no boxes to read, and the door says so rather than answering three zeroed
     *  rectangles. */
    int boxGeometry;
    /**
     * The clip **record**, accumulated per axis from `overflow` (11.1): the boundary in force
     * at a point in the traversal, carried on every operation and every command.
     *
     * Superseded as a *claim* by `clipping` below, which is about the scopes rather than about
     * the record: a build could accumulate this correctly and still have no way to say where one
     * boundary ends. It stays because the record is still what carries the boundary, and a caller
     * asking "can this build represent a clip at all?" is asking a different question from "does
     * it know which scope a drawing is inside?".
     */
    int clipState;
    /**
     * The **decoration** operations: a background and a border side, emitted as records
     * with a colour resolved from the computed style (11.1). **1** since 11.1.
     *
     * Named for what it is rather than for what a reader might hope: this build decides
     * *that* a background will be filled and *which* colour, and records it. It does not
     * fill it. `drawing` below is the field that answers the other question, and keeping
     * the two apart is what stops "a background operation exists" from being read as "a
     * background was painted".
     */
    int decorationOps;
    /**
     * Putting a colour on a surface. **1** since 11.4: a drawing command's rectangle and
     * colour reach a RoboGraphics surface (`PaintDecorationPaint`).
     *
     * The field a caller about to look for a framebuffer should read -- and the one that says
     * this build paints a box's **background**, its **border bands** and, since 11.6, a **text
     * run's glyphs**. It is `backgroundPainting`/`borderPainting`/`textPainting` beside it
     * that say *which* drawings exist, because "a colour reaches a surface" and "every command
     * rasterises" are different claims and only the first is true.
     */
    int drawing;
    /**
     * A display list as a consumable artifact (**11.3**, and **1** since then): the
     * `RoboPaintDisplayList` that resolves the box model into one rectangle per command and
     * enforces the traversal's structural convention, built from a paint list.
     *
     * **1 since 11.3, and 1 while `drawing` was 0 for one release** -- the pair was the point
     * then and still is: the *artifact* a rasteriser consumes and the *drawing* of that artifact
     * into a surface are two facts, and a caller that read only one of them would be wrong about
     * the other. Keeping them apart is also why `raster`, one field down, was allowed to stay 0
     * for eight more releases while `drawing` was 1.
     */
    int displayLists;
    /**
     * **Executing a whole display list into pixels** (11.12), and **1** since this module's
     * 0.12.0 -- but the field was 0 for nine releases and *what changed* is the module above rather
     * than this one.
     *
     * The three *drawing* command kinds have reached a caller's surface since 11.4-11.6 (see
     * `drawing`, `backgroundPainting`, `borderPainting`, `textPainting`, `imagePainting`), and
     * the three structural kinds are not drawings and never will be. What was missing was the
     * subsystem that walks a list *as a frame*: an owned pixel target, a run that executes the whole
     * vocabulary including the two scope pairs, an execution record, byte-determinism and
     * region-limited execution. That subsystem is `RoboRender` (11.12), it sits directly above this
     * module in `DEPENDENCIES.md`, and it drives this module's walk through the bounded door.
     *
     * So read this field as "a caller with a display list can obtain pixels, today, from this
     * build", and read the sentence as the *pair*'s claim rather than this module's: RoboPaint owns
     * what the list means and RoboRender owns what a frame is. What it does **not** claim is
     * anything a rasteriser's harder senses imply -- tiles, damage tracking, a frame scheduler, a
     * compositor, a GPU -- and `RoboRender.h` names each of those as a 0 beside it, which is where a
     * caller deciding what to build next should look.
     */
    int raster;
    /**
     * **Knowing which regions a change made stale** (11.11), and **1**: an owned set of
     * rectangles, each with why it needs repainting, the boundary it was drawn under and
     * whether it is a conservative whole-extent answer -- filled by explicit marks (a
     * rectangle, an old-and-new pair, a box, a subtree, the whole extent), by comparing two
     * paint artifacts, or by reading `PaintStats.styleOnly`, which this module and
     * 11.1 document as its input.
     *
     * The field 11.1 declared and left 0, and the claim it makes is narrower than the word: a
     * region is a promise that a repaint of it is **sufficient**, never that those pixels
     * changed -- every door here widens rather than narrows when it is not certain. It does not
     * claim a partial list *patch* (the display list is rebuilt
     * and the set says how little of it a caller may re-execute), a tile tree, a damage
     * history, frame scheduling or a compositor layer, and it is a *claim about regions* rather
     * than about pixels reaching a screen -- `drawing` is the field for that.
     */
    int paintInvalidation;
    /**
     * **Background painting** (11.4), and **1**: a box's `background-color` reaching its
     * RoboGraphics **border box**.
     *
     * A drawing kind, and one of the two the decoration pass fills. See
     * `RoboPaintDecoration.h` for what a background command carries and why the border box is
     * the rectangle CSS means by it.
     */
    int backgroundPainting;
    /**
     * **Border painting** (11.5), and **1**: each border side whose used width is above zero
     * filled across its **band**. One field beside `backgroundPainting` rather than folded
     * into it, because the two arrived a phase apart and a caller is entitled to know which
     * of a box's decorations a build draws.
     *
     * It claims the **band**, in the side's own colour -- which is CSS's `solid` shape -- and
     * not the seven pattern styles: `dotted`, `dashed`, `double`, `groove`, `ridge`, `inset`
     * and `outset` are counted rather than shaped (`PaintDecorationStats.unshaped`),
     * so "borders are painted" is true of a border's place and colour and not of every
     * border's look. `raster` above is still 0: the structural kinds are not drawings.
     */
    int borderPainting;
    /**
     * **Text painting** (11.6), and **1**: a text run's glyphs reaching a RoboGraphics surface
     * through a caller-supplied face.
     *
     * The third drawing kind, and the one a caller most needs a separate field for: it is the
     * only claim here that needs a **resource** other than the surface, so a build can paint
     * every background and band of a document and no text at all -- with the opposite being
     * impossible -- and this field beside `drawing` is how a caller tells which it got.
     *
     * What it claims is a run's **glyphs on a surface**, not their look: the glyph mode
     * `RoboGraphicsText.h` implements is an outline **stroke**, so this build draws text as
     * 1-pixel outlines rather than as filled or antialiased shapes, and shaping, bidi, fallback
     * and line breaking are all still absent (`PaintDecorationStats` counts the RTL
     * runs it drew in logical order and the code points it drew as `.notdef`).
     */
    int textPainting;
    /**
     * **Clipping** (11.8), and **1**: the boundary a document's `overflow` puts in force is
     * carried by explicit **scopes** -- a `push-clip` command standing after a box's content
     * boundary and a `pop-clip` after its last child -- the builder refuses a command inside a
     * scope that carries a different boundary, and the drawing pass bounds each drawing by the
     * scope it stands inside.
     *
     * The fourth field beside `backgroundPainting`, `borderPainting` and `textPainting`, and
     * the first of the four that is not about a *drawing*: it is about where one stops. It does
     * **not** claim rounded corners, clip paths, masks, filters, scroll containers' clipping or a
     * compositor's clip, and it does not claim `SAVE_STATE`/`RESTORE_STATE` -- the raster rebuilds
     * its clip from the command it is about to draw, so there is no graphics state for such a pair
     * to save. `clipState` above is 11.1's record; this is what 11.8 added above it.
     */
    int clipping;
    /**
     * **Opacity** (11.9), and **1**: an element's `opacity` is accumulated along the path that
     * reaches it, carried by explicit **scopes** -- a `push-opacity` command standing after a
     * box's `begin-node` and a `pop-opacity` before its `end-node` -- and applied to every
     * drawing inside them, on top of the colour's own alpha.
     *
     * The fifth field, the second that is not about a *drawing*, and the first that is about
     * something a drawing is made *of* rather than where it is put. It does **not** claim group
     * opacity: CSS composites an element's whole subtree and applies the opacity to the flattened
     * result, and this build multiplies per drawing (`RoboPaintOpacity.h` argues the deviation in
     * full), which is exact for a subtree whose contents do not overlap. It does not claim
     * `mix-blend-mode`, filters, `backdrop-filter`, masks, transforms or a compositor layer --
     * every one of which needs the offscreen buffer the group form would need as well.
     */
    int opacity;
    /**
     * **Hit testing** (11.10), and **1**: a point in paint coordinates resolves to the layout
     * node that would be drawn there, respecting the painter's order, the boundary its `overflow`
     * puts in force and the geometry RoboLayout concluded.
     *
     * The sixth field, and the first that is not about *drawing* at all: it is the **inverse**
     * question, asked of the same tree and answered by the same records. What it claims is a
     * **node** -- a box or a text run, its geometry, and the two states in force on it -- and the
     * two things a caller must not read into it are that it delivers events (it does not; walking
     * up to the nearest element that could listen is Phase 12's) and that opacity suppresses it
     * (it does not: a node at `opacity: 0` is still the answer, because CSS governs hit testing
     * with `pointer-events` and this build has none).
     */
    int hitTesting;
    /**
     * **Image painting** (11.7), and **1**: a document's `<img>` resource reaching a
     * RoboGraphics surface at the rectangle layout gave its element.
     *
     * The seventh field, and the fourth that is about a *drawing* -- placed at the end rather
     * than beside `backgroundPainting`, `borderPainting` and `textPainting`, where a reader
     * would look for it, because the struct is asserted **positionally** by
     * `paint.conformance.capabilities` and a field inserted in the middle would renumber every
     * field after it. That is a constraint about a test rather than about the claim, and the
     * claim itself is the one a caller about to look for a picture should read.
     *
     * What it claims is deliberately narrow, and the narrowness is the phase's own limit stated
     * where a caller will find it: the resource is one the document **already resolved** (there
     * is no fetch, no URL, no network), the destination is the box's **content box** from
     * RoboLayout (so an image is as large as its CSS says, because intrinsic sizing for replaced
     * elements is layout's work and is not implemented), and what is drawn is the **full source**
     * scaled into that rectangle by the graphics module's nearest-neighbour sampler. It does not
     * claim `object-fit`, cropping, filters, animation or a broken-image presentation -- a
     * document whose image is missing, failed or unsupported simply paints nothing for it, and
     * `PaintStats.imageSkipped` says how many times that happened.
     */
    int imagePainting;
    /**
     * **Shaped glyph runs on the record** (12.16), and **1**: a text operation and the display command it
     * becomes carry the glyphs the recording pass produced -- the face's own glyph ids, each with its
     * placement offset, its device advance, its **cluster** and its source range -- rather than only the code
     * points the run was given.
     *
     * The ninth field, and the fifth that is about a *drawing*: `textPainting` says a run's glyphs reach a
     * surface, and this says **what travels with the run** so that they can. Read the two together: a build can
     * paint text through a `cmap` at draw time (which is exactly what this module did from 11.6 to 12.15 and
     * what the field being 1 now rules out) and still have `textPainting` at 1.
     *
     * What it claims: a glyph is an id in a named face, and the record names the face by a **token** into a table
     * the list owns (`RoboPaintFontTable.h`) -- so a glyph id drawn with the wrong face is refused as a counted
     * miss rather than silently drawn as a different letter, and a display list's glyphs and faces are its
     * **own copies** (`PaintDisplayListGlyphPool`, `PaintDisplayListFontTable`). It does **not** claim that a
     * document names more than one face today: the recorder is given one (`PaintContextSetTextFace`), which is
     * what the token vocabulary makes a *later* phase able to lift rather than what this build already lifts.
     */
    int glyphRuns;
    /**
     * **Text shaped where the document is recorded** (12.16), and **1**: a run's characters become glyphs during
     * the recording pass, once per document, and a replay of the list that results performs no shaping at all --
     * no decoding, no font selection, no GSUB, no GPOS, no kerning, and no `cmap` lookup anywhere in the paint
     * path.
     *
     * Beside `glyphRuns` rather than folded into it because the two answer different questions and a build can
     * have either without the other: a list could carry shaped glyphs that a replay re-derived from the
     * characters (which is what 12.4-12.15's drawing pass did -- shape on every frame), and a recorder could
     * shape without the result being carried in a form a *different* list could draw. This field is the claim
     * that the expensive answer is computed once and **stored**, and it is the one the phase exists for.
     *
     * What it does not claim: a glyph cache, an atlas, batching of compatible runs, subpixel-phase-keyed
     * rasterization, or a multi-face recorder -- `glyphRuns` above names where the face identity reaches, and
     * what is deliberately absent is a *cache* rather than the ability to name one.
     */
    int shapedTextRecording;
} PaintCapabilities;

/** Zero @p capabilities (every feature reported absent). Safe on NULL. */
void PaintCapabilitiesInit(PaintCapabilities *capabilities);

/** Fill @p out with this build's capabilities.
 *  @return RoboOk; RoboInvalidArgument for NULL @p out. */
Result PaintGetCapabilities(PaintCapabilities *out);

/** The version this build was compiled as ("0.4.0"); never NULL. */
const char *PaintVersionString(void);

/**
 * The version, by number.
 * @return RoboOk; RoboInvalidArgument for any NULL output.
 */
Result PaintGetVersion(I32 *outMajor,
                                     I32 *outMinor,
                                     I32 *outPatch);

/**
 * Non-zero when drawing exists in this build.
 *
 * A function rather than only a capability field, because it is the question a caller
 * actually asks before deciding whether to make a surface -- and because a *function*
 * cannot be read past.
 *
 * It answers **1** since 11.4, and the condition the version header wrote down for the
 * flip was exactly this: "a build in which an operation's rectangle and colour reach a
 * RoboGraphics surface". A `draw-background` command's rectangle and colour did, and since
 * 11.5 a `draw-border` command's band and colour do too, through the same pass
 * (`PaintDecorationPaint`). What it does **not** mean is that every command draws
 * something: the three **structural** kinds draw nothing by definition, and the pass counts them
 * (`PaintDecorationStats.skipped`) rather than pretending otherwise. So the two things a
 * caller must not conclude are that a structural command reaches a surface (it does not) or that
 * this door is a frame -- it has no target it owns, no execution record and no region execution;
 * `RenderRasterize` is the door that answers a frame, and since 11.12 it is the one a caller
 * asking "can a document become pixels?" should read.
 */
int PaintDrawingIsImplemented(void);

/**
 * Bring the module up.
 *
 * RoboPaint owns no global state -- no surface list, no display list cache, no
 * per-pass scratch -- so this is a state transition with nothing that can fail beyond
 * being called twice. It is here because every module in the runtime has the same
 * lifecycle, and a caller should not have to know which ones are special.
 *
 * @return RoboOk; RoboAlreadyExists when already initialised.
 */
Result PaintInit(void);

/** Bring the module down. Lists and contexts are the callers' and are not touched.
 *  Safe to call when not initialised. */
void PaintShutdown(void);

/** Non-zero between init and shutdown. */
int PaintIsInitialized(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_H */
