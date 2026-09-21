/*
 * RoboDisplay commands (Phase 11.3).
 *
 * One display command: a **render-ready** record a rasteriser can execute without ever
 * consulting the box model. It is the artifact Phase 11.3 adds above the paint record,
 * and the whole of the difference between the two is **one resolved rectangle** -- plus,
 * since 11.6, the one field a run needs beyond it.
 *
 * A `PaintOperation` (`RoboPaintOperation.h`) carries the box's **three
 * rectangles** -- border, padding, content -- and a **kind** out of a seven-wide
 * vocabulary, and it deliberately leaves one derivation to the consumer: the band a
 * border side occupies is `PaintOperationSideBand`, computed on demand because
 * the operation already carries the two rectangles the band is the difference of. That
 * is the right shape for a *record of what will be drawn*: a record of a decision
 * carries the inputs to that decision.
 *
 * A display command carries the **one rectangle the command acts on, already resolved**:
 *
 *   a background acts on the border box;
 *   a border side acts on its **band** -- the region of the border box outside the
 *   padding box on that side;
 *   a text run acts on the rectangle RoboLayout placed it at -- and that rectangle *is*
 *   its border, padding and content box, because a run has no insets (11.6);
 *   the content boundary acts on the content box;
 *   the node markers act on the border box.
 *
 * So a rasteriser walks a display list and never needs `padding`, never needs a CSS box
 * side, and never needs to know that "background" means "the border box". The derivation
 * happens **once**, at build time (`RoboPaintDisplayListBuilder.h`), which is what makes the
 * duplicate looking records non-duplication: the paint list says which rectangles a
 * decision was made from; the display list says which rectangle to fill.
 *
 * The value that *does* disappear is the padding box, and the disappearance is a
 * statement rather than a loss: the padding box was an input to the band derivation, and
 * once the band is resolved nothing above needs it. The **coordinate space is
 * unchanged** -- a command's rectangle is in paint coordinates, one page space, exactly
 * as the operation's boxes were -- so the numbers a rasteriser receives are still the
 * numbers RoboLayout wrote, with no origin to flip in between.
 *
 * The vocabulary is **eight kinds**, and the shapes worth stating are the two collapses. The
 * four border kinds become one, because their difference (which side) is consumed into the
 * band rectangle: a command that carried a side would be carrying a fact its own geometry
 * already states. And the two clip kinds (11.8) are the one pair whose *payload* is not a
 * rectangle to act on but the boundary the scope establishes -- which is why a clip command
 * carries its clip twice, in the field every command has and in nothing else.
 */
#ifndef ROBO_PAINT_DISPLAY_COMMAND_H
#define ROBO_PAINT_DISPLAY_COMMAND_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSColor.h"

#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutTypes.h"

#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintGeometry.h"
#include "RoboPaint/RoboPaintImage.h"
#include "RoboPaint/RoboPaintOpacity.h"
#include "RoboPaint/RoboPaintText.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What one display command is.
 *
 * A closed vocabulary in the order a linear consumer meets it, and the order is the
 * paint order: enter a node, its own drawing, its content boundary, its contents, leave
 * it. The three **structural** kinds (`BEGIN_NODE`, `CONTENT`, `END_NODE`) are emitted for
 * **every** placed box and carry no colour; the two drawing kinds (`DRAW_BACKGROUND`,
 * `DRAW_BORDER`) are emitted **only when they draw**, exactly as their paint-record
 * counterparts are -- so a display list has the same regular skeleton and the same
 * document-dependent length as the record list it was built from.
 */
typedef enum PaintDisplayCommandKind {
    /** Nothing: a cleared command. Never appended to a list. */
    RoboPaintDisplayCmdNone = 0,
    /** Enter a node: begins one box's commands. Draws nothing. */
    RoboPaintDisplayCmdBeginNode = 1,
    /** Fill a rectangle with a colour: a box's background, over its border box. */
    RoboPaintDisplayCmdDrawBackground = 2,
    /**
     * Fill a rectangle with a colour: one border side's **band**.
     *
     * A single kind rather than the four the paint record uses, and the four collapse
     * because the fact that distinguished them -- which side -- has been consumed into
     * the band rectangle. A command that also carried a side would be carrying a fact its
     * own geometry already states.
     */
    RoboPaintDisplayCmdDrawBorder = 3,
    /** A node's own drawing is over and its contents begin. Draws nothing. */
    RoboPaintDisplayCmdContent = 4,
    /**
     * Enter a **clip scope** (11.8): the boundary from here until the matching `POP_CLIP`.
     *
     * The `PUSH_CLIP` record's counterpart, and the command that turned the clip from a field
     * travelling on every drawing into a **scope the stream states**. It carries the effective
     * clip -- what is in force *inside* the scope, after the intersection -- so a consumer
     * walks the list, pushes the value and knows the boundary without reaching for a layout
     * tree or re-deriving an intersection; and it carries the box's padding box as its
     * rectangle, which is the boundary the scope was derived from and the answer to "where did
     * this come from?" when a dump is being read.
     *
     * It appears **after the box's `CONTENT` and before its first child**, and the matching
     * `POP_CLIP` appears after the last child and before `END_NODE` -- so the drawing commands
     * for a box's own background and border are outside the scope the box itself establishes,
     * which is CSS's arrangement: the boundary clips the *contents*.
     */
    RoboPaintDisplayCmdPushClip = 5,
    /**
     * Draw one text run's glyphs (11.6).
     *
     * The `TEXT` record's counterpart, and the first drawing here that is not a rectangle
     * filled with a colour: what it acts on is the run's rectangle (the `rect`), **where on
     * that rectangle the text sits** (the baseline, as an offset from the rect's top) and
     * **which characters** (a span into the list's own pool).
     *
     * It appears **inside the box whose element owns the text node**, after that box's
     * `CONTENT`, because a text object is a child of that box and a run has no box of its
     * own to open. So a consumer that reads a box's own painting as
     * `[BEGIN_NODE .. CONTENT]` and its contents as `(CONTENT .. END_NODE)` reads a
     * `DRAW_TEXT` as one of the contents -- which is exactly what the document says.
     */
    RoboPaintDisplayCmdDrawText = 6,
    /**
     * Leave the clip scope the matching `PUSH_CLIP` opened (11.8).
     *
     * It carries the **same clip its push carried**, so the pair is self-identifying: the
     * builder refuses a pop whose clip, node or depth does not match the innermost open scope,
     * exactly as it refuses a `CONTENT` under the wrong box, and a consumer replaying the list
     * restores the enclosing boundary by popping rather than by consulting the payload.
     *
     * There is deliberately **no `SAVE_STATE`/`RESTORE_STATE` pair** around it. A rasteriser in
     * this build rebuilds its clip from the command it is about to draw (`RoboPaintDecoration.c`),
     * so there is no graphics state to save and a save/restore pair would be two commands whose
     * only effect is to be skipped -- which is the shape `RoboPaintTypes.h` refuses everywhere:
     * a record a consumer can only ignore. A later pass with real graphics state adds the pair
     * as one more enumerator and one more row.
     */
    RoboPaintDisplayCmdPopClip = 7,
    /** Leave a node: closes one box's commands. Draws nothing. */
    RoboPaintDisplayCmdEndNode = 8,
    /**
     * Enter an **opacity scope** (11.9): the strength in force from here until the matching
     * `POP_OPACITY`.
     *
     * The `PUSH_OPACITY` record's counterpart, and the command that makes an element's opacity a
     * **scope the stream states** rather than a field on every descendant's drawing. It carries
     * the effective opacity -- the product, already accumulated -- so a consumer walks the list,
     * pushes the value and knows the strength without reaching for a tree; and it carries the
     * box's border box as its rectangle, which is where the scope came from and the answer to "who
     * declared this?" when a dump is being read.
     *
     * It appears **after the box's `BEGIN_NODE` and before its first drawing command**, which is
     * where it differs from `PUSH_CLIP` and for a reason CSS states: an element's opacity applies
     * to its own background and border as much as to its contents, so the scope must open *before*
     * the box draws anything, where a clip boundary applies only to the contents and opens after
     * the content boundary.
     */
    RoboPaintDisplayCmdPushOpacity = 9,
    /**
     * Leave the opacity scope the matching `PUSH_OPACITY` opened (11.9).
     *
     * It carries the **same opacity its push carried**, so the pair is self-identifying: the
     * builder refuses a pop whose opacity, node or depth does not match the innermost open scope, 
     * exactly as it refuses a mismatched clip scope. A consumer replaying the list restores the
     * enclosing strength by popping rather than by consulting the payload.
     *
     * Like the clip pair, there is deliberately **no `SAVE_STATE`/`RESTORE_STATE`** around it: the
     * drawing pass sets the raster's opacity from the command it is about to draw, so there is no
     * graphics state to save and a save/restore pair would be two commands whose only effect is to
     * be skipped.
     */
    RoboPaintDisplayCmdPopOpacity = 10,
    /**
     * Draw a decoded image into the rectangle layout gave its element (11.7).
     *
     * The `IMAGE` record's counterpart, and the second drawing kind here whose `rect` is not a
     * shape this module derived: it is the box's **content box**, because what an image draws
     * *is* its element's content and the content box is where content goes. It is resolved here,
     * once, from the record's own boxes -- so the command carries one destination rectangle and
     * a rasteriser never touches the box model (which is the whole of what this artifact is for).
     *
     * It carries no colour, and that is the one structural difference between it and the three
     * drawings beside it: the pixels are the **resource's**, so the thing it carries instead is a
     * payload (`RoboPaintImage.h`) -- the retained resource, the rectangle *of the image* to
     * sample, and the resource's facts at snapshot time. `uses_color` below is the predicate that
     * says so, and the validity rule reads it rather than treating this kind as a special case.
     *
     * It appears **inside the node that owns the image element**, after that node's border
     * commands and before its `CONTENT` -- CSS's paint order for a replaced element, background
     * then border then content -- so a consumer reading a box's own painting as
     * `[BEGIN_NODE .. CONTENT]` finds the image's drawing there and not among the node's
     * contents. That is the opposite placement from `DRAW_TEXT`, and for the reason the record
     * layer states: a run is a child object of its box, an image is its element's content.
     */
    RoboPaintDisplayCmdDrawImage = 11
} PaintDisplayCommandKind;

/** Number of defined display command kinds. */
#define RoboPaintDisplayCmdKindCount 12u

/** Non-zero if @p kind is a defined display command kind. */
int PaintDisplayCommandKindIsKnown(PaintDisplayCommandKind kind);

/** Human-readable kind name; never NULL; "unknown" when undefined. */
const char *PaintDisplayCommandKindName(PaintDisplayCommandKind kind);

/**
 * Non-zero when @p kind **puts a pixel down**: `DRAW_BACKGROUND`, `DRAW_BORDER`, `DRAW_TEXT`
 * or, since 11.7, `DRAW_IMAGE`. The predicate the builder uses, the drawing pass asks, and
 * one half of the split the validity rule is stated on.
 */
int PaintDisplayCommandKindDraws(PaintDisplayCommandKind kind);

/**
 * Non-zero when @p kind's appearance is **a colour this module resolved**: the background, a
 * border band (11.5) and a text run (11.6).
 *
 * The other half of the validity split, and the reason it is not simply "draws": 11.7 added a
 * drawing kind whose appearance is a **resource's pixels**, so a rule stated on `draws` would be
 * asking `DRAW_IMAGE` for a colour it cannot have. Stated positively, the next payload kind -- a
 * surface, a gradient, a video frame -- says no here and carries its own payload, and none of
 * them has to be added to an exception list in the validity rule.
 *
 * A cleared command (`NONE`) and a structural kind answer no, which is what makes the predicate
 * usable on both sides of the split: a kind that uses a colour must carry a drawable one, and a
 * kind that does not must carry `none`.
 */
int PaintDisplayCommandKindUsesColor(PaintDisplayCommandKind kind);

/** Non-zero when @p kind is **structural**: `BEGIN_NODE`, `CONTENT`, `END_NODE`. The
 *  exact complement of @ref PaintDisplayCommandKindDraws above `NONE`. */
int PaintDisplayCommandKindIsStructural(PaintDisplayCommandKind kind);/** Non-zero when @p kind is a **node marker**: `BEGIN_NODE`/`END_NODE`. Narrower than
 *  @ref PaintDisplayCommandKindIsStructural, which also covers `CONTENT`.
 */
int PaintDisplayCommandKindIsMarker(PaintDisplayCommandKind kind);

/**
 * Non-zero when @p kind is one of the two **clip scope** commands (11.8): `PUSH_CLIP` or
 * `POP_CLIP`.
 *
 * The predicate the decoration pass and the structural check both ask, and the reason it is
 * here rather than repeated: the pair is a *scope*, so every consumer that maintains a stack
 * needs to recognise both halves, and a second comparison written at each site is a second
 * place for one half to be forgotten.
 */
int PaintDisplayCommandKindIsClip(PaintDisplayCommandKind kind);

/**
 * Non-zero when @p kind is one of the two **opacity scope** commands (11.9): `PUSH_OPACITY` or
 * `POP_OPACITY`.
 *
 * The clip pair's sibling, and the predicate the drawing pass and the structural check both ask
 * for the same reason: the pair is a *scope*, so a consumer maintaining a stack has to recognise
 * both halves, and a second comparison at each site is a second place for one half to be forgotten.
 */
int PaintDisplayCommandKindIsOpacity(PaintDisplayCommandKind kind);

/**
 * One command of a display list.
 *
 * A value record with no pointers, owned by nobody: safe to copy, to embed and to put on
 * a surface consumer's stack. It carries:
 *
 *   `rect`  the **one rectangle the command acts on**, in paint coordinates, already
 *           resolved from the operation's box model (see the header comment);
 *   `clip`  the boundary in force, carried through unchanged from the operation, because
 *           a rasteriser applies it from the command rather than from a separate op;
 *   `color` the fill colour for a drawing command, and `none` for a structural one;
 *   `style` which of the ten border styles a band draws (11.5), and `none` for every
 *           kind that is not a border. It is here because this artifact is
 *           **render-ready**: a rasteriser must be able to produce the drawn shape from
 *           the command alone, and a border's shape is its style. A command that carried
 *           the band and the colour but not the style would be one its consumer could
 *           only draw as `solid` and could not know it was guessing;
 *   `text`  the run a `DRAW_TEXT` acts on (11.6): the span naming its characters in the
 *           list's pool, the baseline's offset from `rect`'s top, and the font inputs the
 *           run was measured with -- and a cleared record for every other kind, which is
 *           the rule `style` follows one field along. It is the same sub-record the paint
 *           operation carries, copied through rather than rebuilt, so "the size it was
 *           measured at is the size it will be drawn at" is a copy rather than an
 *           agreement to maintain;
 *   `image` the payload a `DRAW_IMAGE` acts on (11.7): the **retained** resource, the
 *           rectangle of it to sample, and the resource's identity, dimensions and format
 *           at snapshot time -- and a cleared payload for every other kind, the rule `text`
 *           follows one field along. It is the same sub-record the paint operation carries, for
 *           the same reason: a display command is supposed to be render-ready, and a resource
 *           read back out of a document at draw time would not be.
 *
 * The characters themselves are **not** in the command, and that is the one place this
 * artifact is not self-contained: they live in a pool the display list owns, and the
 * command carries the span that names them (`RoboPaintText.h`). A command is thus as
 * pointer-free as it was before this phase -- two numbers in place of a pointer, exactly
 * the trade the paint record made -- and the list is still safe to copy, to discard and
 * to rebuild.
 *
 * `node` and `depth` are the box's identity and place, carried so a command is still
 * checkable against the tree it came from and so the tree's shape is recoverable from a
 * flat list without consulting anything mutable.
 */
typedef struct PaintDisplayCommand {
    /** What this is. `NONE` is a cleared record and is never in a built list. */
    PaintDisplayCommandKind kind;
    /** The layout object this command acts on, by id. Never `NONE` for a live
     *  command. */
    LayoutNodeId node;
    /** How deep in the tree the node is: 0 for the root. */
    size_t depth;
    /** The **one rectangle** the command acts on, in paint coordinates. */
    LayoutRect rect;
    /** The boundary in force, carried from the operation. */
    PaintClip clip;
    /**
     * The **opacity in force** (11.9), copied from the operation: the factor a rasteriser
     * composites this command's drawing with.
     *
     * Carried on every kind, like @ref clip and for the same reason -- a consumer reading a flat
     * sequence must be able to know what is in force without replaying it -- and it is also what the
     * command's own `push-opacity`/`pop-opacity` payload is, so the pair is self-identifying. A
     * cleared command carries the fully **opaque** value, which is the state in which a document that
     * declares no opacity draws.
     */
    PaintOpacity opacity;
    /** The colour a drawing command fills with; `none` for a structural one. */
    CssColor color;
    /** The `border-style` a band draws (11.5): meaningful for `DRAW_BORDER`, and required
     *  to be `NONE` for every other kind. */
    CssBorderStyle style;
    /** The run a `DRAW_TEXT` acts on (11.6): the span, the baseline offset and the font
     *  inputs, copied from the operation. A cleared record for every other kind. */
    PaintTextRun text;
    /**
     * The image a `DRAW_IMAGE` acts on (11.7): the resource, the source rectangle and the
     * resource's facts, copied from the operation. A cleared payload for every other kind.
     *
     * The resource is **retained by the list that holds this command** -- the builder takes the
     * reference when it stores the command and `PaintDisplayListClear`/`_destroy` gives it
     * back -- so a command's payload and the record it was built from are independent, and
     * destroying the record list cannot change a pixel this list draws. That is the same rule
     * the record list's own append states, one artifact up.
     */
    PaintImage image;
} PaintDisplayCommand;

/** Clear @p command to a `NONE` marker with no geometry. Safe on NULL. */
void PaintDisplayCommandInit(PaintDisplayCommand *command);

/**
 * Non-zero when @p command is a command this build may hand to a consumer.
 *
 * The rules are the record's shape rather than a taste, and each refuses a combination
 * that would otherwise travel one stage too far:
 *
 *   a **known kind**, and never `NONE`;
 *   a **live node id**;
 *   a **rectangle with a non-negative size** (empty is fine -- a zero-height box is a
 *   statement about the document);
 *   a **valid clip**;
 *   the colour rule: `none` for a **structural** command, and a colour a rasteriser
 *   can use -- resolved, or `currentColor` this layer could not resolve -- for a
 *   **drawing** one. `none` on a drawing command is refused because a fill with no colour
 *   draws nothing;
 *   the style rule (11.5): a `draw-border` carries a style that generates a border,
 *   and every other kind carries `NONE`. A `draw-background` with a style, or a band with
 *   `none`, is a command whose fields disagree about what it is;
 *   the text rule (11.6): a `draw-text` carries a **run**, and every other kind a
 *   cleared one;
 *   and the image rule (11.7), the text rule's shape for a payload that is not characters: a
 *   `draw-image` carries an **image** this build may draw -- a resource with a surface, positive
 *   intrinsic dimensions and a source rectangle inside it -- and every other kind a cleared one.
 *   Its colour is required to be `none`, which the split on
 *   `PaintDisplayCommandKindUsesColor` enforces rather than a special case here.
 */
int PaintDisplayCommandIsValid(const PaintDisplayCommand *command);

/** Field-by-field equality of every field, including the rectangle and the clip. */
int PaintDisplayCommandEqual(const PaintDisplayCommand *a,
                               const PaintDisplayCommand *b);

/** The kind (`RoboPaintDisplayCmdNone` for NULL). */
PaintDisplayCommandKind MakePaintDisplayCommandKind(const PaintDisplayCommand *command);

/** The node (`RoboLayoutNodeIdNone` for NULL). */
LayoutNodeId PaintDisplayCommandNode(const PaintDisplayCommand *command);

/** The depth (0 for NULL). */
size_t PaintDisplayCommandDepth(const PaintDisplayCommand *command);

/** The rectangle, by value. Zeros for NULL. */
LayoutRect PaintDisplayCommandRect(const PaintDisplayCommand *command);

/** The clip, by value. A cleared clip for NULL. */
void PaintDisplayCommandClip(const PaintDisplayCommand *command,
                              PaintClip *out);

/** The opacity in force, by value. The fully opaque record for NULL -- so a consumer that
 *  reads a command without asking anything draws it at full strength, which is what a document
 *  that declared no opacity means. */
void PaintDisplayCommandOpacity(const PaintDisplayCommand *command,
                                       PaintOpacity *out);

/** The colour, by value. `none` for NULL. */
CssColor PaintDisplayCommandColor(const PaintDisplayCommand *command);

/** The `border-style`, by value. `NONE` for NULL and for every kind that is not a
 *  border band. */
CssBorderStyle PaintDisplayCommandStyle(const PaintDisplayCommand *command);

/** The text run, by value. A cleared run for NULL and for every kind that is not
 *  `DRAW_TEXT`. */
PaintTextRun PaintDisplayCommandText(const PaintDisplayCommand *command);

/**
 * The image payload, by value, **without taking a reference on the resource**.
 *
 * A cleared payload for NULL and for every kind that is not `DRAW_IMAGE`, which is the shape
 * @ref PaintDisplayCommandText has: a caller that did not ask the kind gets "no
 * resource" rather than a leftover. The resource it names is kept alive by the list that holds
 * the command, so a caller that wants the payload to outlive the list calls
 * `PaintImageRetain` on a copy -- the same arrangement the record layer's accessor states.
 */
PaintImage PaintDisplayCommandImage(const PaintDisplayCommand *command);

/** Non-zero when @p command is **structural**. Safe on NULL (0). */
int PaintDisplayCommandIsStructural(const PaintDisplayCommand *command);

/** Non-zero when @p command is a `BEGIN_NODE`/`END_NODE` marker. Safe on NULL (0). */
int PaintDisplayCommandIsMarker(const PaintDisplayCommand *command);

/**
 * Non-zero when @p command draws something: a background, a border band or a text run. Safe
 * on NULL (0).
 */
int PaintDisplayCommandIsDrawing(const PaintDisplayCommand *command);

/** Non-zero when @p command opens or closes a clip scope (11.8). Safe on NULL (0). */
int PaintDisplayCommandIsClip(const PaintDisplayCommand *command);

/** Non-zero when @p command opens or closes an opacity scope (11.9). Safe on NULL (0). */
int PaintDisplayCommandIsOpacity(const PaintDisplayCommand *command);

/**
 * Write a one-line description of @p command into @p buffer.
 *
 * The developer's door, beside the record for the same reason the operation's writer is:
 * a failing assertion wants *one* command rendered. `"draw-background node=3 depth=1
 * rect=0,0,100,40 clip=visible/visible color=#ff0000ff"`.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument; RoboNotSupported when
 *         the text does not fit, in which case @p outLength receives the length it
 *         would have needed.
 */
Result PaintDisplayCommandWrite(const PaintDisplayCommand *command,
                                        char *buffer,
                                        size_t capacity,
                                        size_t *outLength,
                                        PaintDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_DISPLAY_COMMAND_H */
