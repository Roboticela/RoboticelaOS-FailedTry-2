/*
 * RoboDisplay commands (Phase 11.3).
 *
 * One display command: a **render-ready** record a rasteriser can execute without ever
 * consulting the box model. It is the artifact Phase 11.3 adds above the paint record,
 * and the whole of the difference between the two is one field.
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
 * The vocabulary is **five kinds**, one narrower than the paint record's seven, because
 * the four border kinds have to collapse: their differences (which side) are consumed
 * into the band rectangle, and a command that carried a side would be carrying a fact
 * its own geometry already states.
 */
#ifndef ROBO_PAINT_DISPLAY_COMMAND_H
#define ROBO_PAINT_DISPLAY_COMMAND_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSColor.h"

#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutTypes.h"

#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintGeometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What one display command is.
 *
 * A closed vocabulary in the order a linear consumer meets it, and the order is the
 * paint order: enter a node, its own drawing, its content boundary, its contents, leave
 * it. The three structural kinds (`BEGIN_NODE`, `CONTENT`, `END_NODE`) are emitted for
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
    /** Leave a node: closes one box's commands. Draws nothing. */
    ROBO_PAINT_DISPLAY_CMD_END_NODE = 5
} robo_paint_display_command_kind_t;

/** Number of defined display command kinds. */
#define ROBO_PAINT_DISPLAY_CMD_KIND_COUNT 6u

/** Non-zero if @p kind is a defined display command kind. */
int robo_paint_display_command_kind_is_known(robo_paint_display_command_kind_t kind);

/** Human-readable kind name; never NULL; "unknown" when undefined. */
const char *robo_paint_display_command_kind_name(robo_paint_display_command_kind_t kind);

/**
 * Non-zero when @p kind **puts a pixel down**: `DRAW_BACKGROUND` or `DRAW_BORDER`. The
 * predicate the builder uses, and the drawing half of the validity rule (<-> a drawing
 * kind carries a colour a rasteriser can use, a structural kind carries `none`).
 */
int robo_paint_display_command_kind_draws(robo_paint_display_command_kind_t kind);

/** Non-zero when @p kind is **structural**: `BEGIN_NODE`, `CONTENT`, `END_NODE`. The
 *  exact complement of @ref robo_paint_display_command_kind_draws above `NONE`. */
int robo_paint_display_command_kind_is_structural(robo_paint_display_command_kind_t kind);

/** Non-zero when @p kind is a **node marker**: `BEGIN_NODE`/`END_NODE`. Narrower than
 *  @ref robo_paint_display_command_kind_is_structural, which also covers `CONTENT`. */
int robo_paint_display_command_kind_is_marker(robo_paint_display_command_kind_t kind);

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
 *           only draw as `solid` and could not know it was guessing.
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
 *   and the style rule (11.5): a `draw-border` carries a style that generates a border,
 *   and every other kind carries `NONE`. A `draw-background` with a style, or a band with
 *   `none`, is a command whose fields disagree about what it is.
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

/** Non-zero when @p command is **structural**. Safe on NULL (0). */
int robo_paint_display_command_is_structural(const robo_paint_display_command_t *command);

/** Non-zero when @p command is a `BEGIN_NODE`/`END_NODE` marker. Safe on NULL (0). */
int robo_paint_display_command_is_marker(const robo_paint_display_command_t *command);

/** Non-zero when @p command draws something: a background or a border band. Safe on
 *  NULL (0). */
int robo_paint_display_command_is_drawing(const robo_paint_display_command_t *command);

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
