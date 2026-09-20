/*
 * RoboDisplay commands (Phase 11.3).
 *
 * One display command: a **render-ready** record a rasteriser can execute without ever
 * consulting the box model. It is the artifact Phase 11.3 adds above the paint record,
 * and the whole of the difference between the two is **one resolved rectangle** -- plus,
 * since 11.6, the one field a run needs beyond it.
 *
 * A `robo_paint_operation_t` (`RoboPaintOperation.h`) carries the box's **three
 * rectangles** -- border, padding, content -- and a **kind** out of a seven-wide
 * vocabulary, and it deliberately leaves one derivation to the consumer: the band a
 * border side occupies is `robo_paint_operation_side_band`, computed on demand because
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
typedef enum robo_paint_display_command_kind {
    /** Nothing: a cleared command. Never appended to a list. */
    ROBO_PAINT_DISPLAY_CMD_NONE = 0,
    /** Enter a node: begins one box's commands. Draws nothing. */
    ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE = 1,
    /** Fill a rectangle with a colour: a box's background, over its border box. */
    ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND = 2,
    /**
     * Fill a rectangle with a colour: one border side's **band**.
     *
     * A single kind rather than the four the paint record uses, and the four collapse
     * because the fact that distinguished them -- which side -- has been consumed into
     * the band rectangle. A command that also carried a side would be carrying a fact its
     * own geometry already states.
     */
    ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER = 3,
    /** A node's own drawing is over and its contents begin. Draws nothing. */
    ROBO_PAINT_DISPLAY_CMD_CONTENT = 4,
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
    ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP = 5,
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
    ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT = 6,
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
    ROBO_PAINT_DISPLAY_CMD_POP_CLIP = 7,
    /** Leave a node: closes one box's commands. Draws nothing. */
    ROBO_PAINT_DISPLAY_CMD_END_NODE = 8
} robo_paint_display_command_kind_t;

/** Number of defined display command kinds. */
#define ROBO_PAINT_DISPLAY_CMD_KIND_COUNT 9u

/** Non-zero if @p kind is a defined display command kind. */
int robo_paint_display_command_kind_is_known(robo_paint_display_command_kind_t kind);

/** Human-readable kind name; never NULL; "unknown" when undefined. */
const char *robo_paint_display_command_kind_name(robo_paint_display_command_kind_t kind);

/**
 * Non-zero when @p kind **puts a pixel down**: `DRAW_BACKGROUND`, `DRAW_BORDER` or
 * `DRAW_TEXT`. The predicate the builder uses, the drawing pass asks, and the drawing half
 * of the validity rule (<-> a drawing kind carries a colour a rasteriser can use, a
 * structural kind carries `none`).
 */
int robo_paint_display_command_kind_draws(robo_paint_display_command_kind_t kind);

/** Non-zero when @p kind is **structural**: `BEGIN_NODE`, `CONTENT`, `END_NODE`. The
 *  exact complement of @ref robo_paint_display_command_kind_draws above `NONE`. */
int robo_paint_display_command_kind_is_structural(robo_paint_display_command_kind_t kind);/** Non-zero when @p kind is a **node marker**: `BEGIN_NODE`/`END_NODE`. Narrower than
 *  @ref robo_paint_display_command_kind_is_structural, which also covers `CONTENT`.
 */
int robo_paint_display_command_kind_is_marker(robo_paint_display_command_kind_t kind);

/**
 * Non-zero when @p kind is one of the two **clip scope** commands (11.8): `PUSH_CLIP` or
 * `POP_CLIP`.
 *
 * The predicate the decoration pass and the structural check both ask, and the reason it is
 * here rather than repeated: the pair is a *scope*, so every consumer that maintains a stack
 * needs to recognise both halves, and a second comparison written at each site is a second
 * place for one half to be forgotten.
 */
int robo_paint_display_command_kind_is_clip(robo_paint_display_command_kind_t kind);

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
 *           agreement to maintain.
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
typedef struct robo_paint_display_command {
    /** What this is. `NONE` is a cleared record and is never in a built list. */
    robo_paint_display_command_kind_t kind;
    /** The layout object this command acts on, by id. Never `NONE` for a live
     *  command. */
    robo_layout_node_id_t node;
    /** How deep in the tree the node is: 0 for the root. */
    size_t depth;
    /** The **one rectangle** the command acts on, in paint coordinates. */
    robo_layout_rect_t rect;
    /** The boundary in force, carried from the operation. */
    robo_paint_clip_t clip;
    /** The colour a drawing command fills with; `none` for a structural one. */
    robo_css_color_t color;
    /** The `border-style` a band draws (11.5): meaningful for `DRAW_BORDER`, and required
     *  to be `NONE` for every other kind. */
    robo_css_border_style_t style;
    /** The run a `DRAW_TEXT` acts on (11.6): the span, the baseline offset and the font
     *  inputs, copied from the operation. A cleared record for every other kind. */
    robo_paint_text_run_t text;
} robo_paint_display_command_t;

/** Clear @p command to a `NONE` marker with no geometry. Safe on NULL. */
void robo_paint_display_command_init(robo_paint_display_command_t *command);

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
 *   and the text rule (11.6): a `draw-text` carries a **run**, and every other kind a
 *   cleared one.
 */
int robo_paint_display_command_is_valid(const robo_paint_display_command_t *command);

/** Field-by-field equality of every field, including the rectangle and the clip. */
int robo_paint_display_command_equal(const robo_paint_display_command_t *a,
                               const robo_paint_display_command_t *b);

/** The kind (`ROBO_PAINT_DISPLAY_CMD_NONE` for NULL). */
robo_paint_display_command_kind_t robo_paint_display_command_kind(const robo_paint_display_command_t *command);

/** The node (`ROBO_LAYOUT_NODE_ID_NONE` for NULL). */
robo_layout_node_id_t robo_paint_display_command_node(const robo_paint_display_command_t *command);

/** The depth (0 for NULL). */
size_t robo_paint_display_command_depth(const robo_paint_display_command_t *command);

/** The rectangle, by value. Zeros for NULL. */
robo_layout_rect_t robo_paint_display_command_rect(const robo_paint_display_command_t *command);

/** The clip, by value. A cleared clip for NULL. */
void robo_paint_display_command_clip(const robo_paint_display_command_t *command,
                              robo_paint_clip_t *out);

/** The colour, by value. `none` for NULL. */
robo_css_color_t robo_paint_display_command_color(const robo_paint_display_command_t *command);

/** The `border-style`, by value. `NONE` for NULL and for every kind that is not a
 *  border band. */
robo_css_border_style_t robo_paint_display_command_style(const robo_paint_display_command_t *command);

/** The text run, by value. A cleared run for NULL and for every kind that is not
 *  `DRAW_TEXT`. */
robo_paint_text_run_t robo_paint_display_command_text(const robo_paint_display_command_t *command);

/** Non-zero when @p command is **structural**. Safe on NULL (0). */
int robo_paint_display_command_is_structural(const robo_paint_display_command_t *command);

/** Non-zero when @p command is a `BEGIN_NODE`/`END_NODE` marker. Safe on NULL (0). */
int robo_paint_display_command_is_marker(const robo_paint_display_command_t *command);

/**
 * Non-zero when @p command draws something: a background, a border band or a text run. Safe
 * on NULL (0).
 */
int robo_paint_display_command_is_drawing(const robo_paint_display_command_t *command);

/** Non-zero when @p command opens or closes a clip scope (11.8). Safe on NULL (0). */
int robo_paint_display_command_is_clip(const robo_paint_display_command_t *command);

/**
 * Write a one-line description of @p command into @p buffer.
 *
 * The developer's door, beside the record for the same reason the operation's writer is:
 * a failing assertion wants *one* command rendered. `"draw-background node=3 depth=1
 * rect=0,0,100,40 clip=visible/visible color=#ff0000ff"`.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument; ROBO_NOT_SUPPORTED when
 *         the text does not fit, in which case @p out_length receives the length it
 *         would have needed.
 */
robo_result_t robo_paint_display_command_write(const robo_paint_display_command_t *command,
                                        char *buffer,
                                        size_t capacity,
                                        size_t *out_length,
                                        robo_paint_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_DISPLAY_COMMAND_H */
