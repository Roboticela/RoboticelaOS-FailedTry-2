/*
 * RoboDisplay commands (Phase 11.3) — see RoboPaintDisplayCommand.h.
 *
 * The vocabulary table, the record, the validity rule the list and the builder share, and
 * the one-line writer. The vocabulary is a table for the reason RoboPaint's is: a row per
 * enumerator means the compiler counts the rows against the count, so an enumerator added
 * without a name is a compile-time omission rather than a value that prints "unknown" in a
 * bug report.
 */

#include "RoboPaint/RoboPaintDisplayCommand.h"

#include "RoboPaintInternal.h"

#include <string.h>

/** Spaces the kind name is padded to, so that a two-list diff lines up. */
#define ROBO_PAINT_DISPLAY_NAME_COLUMN 16u

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/** One command kind: the enumerator and its name. */
typedef struct display_kind_row {
    robo_paint_display_command_kind_t kind;
    const char *name;
} display_kind_row_t;

static const display_kind_row_t display_kind_rows[ROBO_PAINT_DISPLAY_CMD_KIND_COUNT] = {
    { ROBO_PAINT_DISPLAY_CMD_NONE, "none" },
    { ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE, "begin-node" },
    { ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, "draw-background" },
    { ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER, "draw-border" },
    { ROBO_PAINT_DISPLAY_CMD_CONTENT, "content" },
    { ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT, "draw-text" },
    { ROBO_PAINT_DISPLAY_CMD_END_NODE, "end-node" }
};

/** The row for @p kind, or NULL when @p kind names none. */
static const display_kind_row_t *display_kind_row_of(robo_paint_display_command_kind_t kind)
{
    size_t index;

    for (index = 0u; index < (size_t)ROBO_PAINT_DISPLAY_CMD_KIND_COUNT; index++) {
        if (display_kind_rows[index].kind == kind) {
            return &display_kind_rows[index];
        }
    }
    return NULL;
}

int robo_paint_display_command_kind_is_known(robo_paint_display_command_kind_t kind)
{
    return display_kind_row_of(kind) != NULL;
}

const char *robo_paint_display_command_kind_name(robo_paint_display_command_kind_t kind)
{
    const display_kind_row_t *row = display_kind_row_of(kind);

    return row ? row->name : "unknown";
}

int robo_paint_display_command_kind_draws(robo_paint_display_command_kind_t kind)
{
    return kind == ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND ||
           kind == ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER ||
           kind == ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT;
}

int robo_paint_display_command_kind_is_structural(robo_paint_display_command_kind_t kind)
{
    /*
     * The complement of drawing above `NONE`, written as that complement rather than as a
     * second list of three enumerators, for the reason RoboPaint restates it: a list would
     * be a second place to add the next kind, and the day the two disagreed a command
     * would be neither drawing nor structural and the validity rule would have no branch
     * for it. `NONE` is excluded because a cleared record is not a command.
     */
    if (!robo_paint_display_command_kind_is_known(kind) || kind == ROBO_PAINT_DISPLAY_CMD_NONE) {
        return 0;
    }
    return !robo_paint_display_command_kind_draws(kind);
}

int robo_paint_display_command_kind_is_marker(robo_paint_display_command_kind_t kind)
{
    return kind == ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE || kind == ROBO_PAINT_DISPLAY_CMD_END_NODE;
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

void robo_paint_display_command_init(robo_paint_display_command_t *command)
{
    if (!command) {
        return;
    }
    memset(command, 0, sizeof(*command));
    command->kind = ROBO_PAINT_DISPLAY_CMD_NONE;
    command->node = ROBO_LAYOUT_NODE_ID_NONE;
    command->rect = robo_gfx_rect(0, 0, 0, 0);
    robo_paint_clip_init(&command->clip);
    command->color = robo_css_color_none();
    command->style = ROBO_CSS_BORDER_STYLE_NONE;
    /* And no text run, which is what every kind except `draw-text` requires -- so "nobody
     * set it" and "this command draws no characters" are one state, as they are for the
     * style and the colour. */
    robo_paint_text_run_init(&command->text);
}

int robo_paint_display_command_is_valid(const robo_paint_display_command_t *command)
{
    if (!command) {
        return 0;
    }
    if (!robo_paint_display_command_kind_is_known(command->kind)) {
        return 0;
    }
    if (command->kind == ROBO_PAINT_DISPLAY_CMD_NONE) {
        /* A cleared record is a state, not a command. */
        return 0;
    }
    if (command->node == ROBO_LAYOUT_NODE_ID_NONE) {
        return 0;
    }
    /* A rectangle is a rectangle: an empty one is fine (a zero-height box is a statement
     * about the document), a negative extent is arithmetic that went wrong one level
     * down. */
    if (command->rect.width < 0 || command->rect.height < 0) {
        return 0;
    }
    if (!robo_paint_clip_is_valid(&command->clip)) {
        return 0;
    }
    /*
     * The colour rule, split on *drawing* rather than on *marker* so that `CONTENT` -- a
     * structural command that is not a node marker -- is held to the no-colour side rather
     * than asked for a colour it has no use for. A structural command must carry `none`;
     * a drawing command must carry a colour a rasteriser can use.
     */
    if (!robo_paint_display_command_kind_draws(command->kind)) {
        return command->color.kind == ROBO_CSS_COLOR_NONE &&
               command->style == ROBO_CSS_BORDER_STYLE_NONE &&
               !robo_paint_text_run_is_valid(&command->text);
    }
    if (!robo_css_color_is_valid(command->color)) {
        return 0;
    }
    /*
     * The style rule, one field along from the colour's and enforced from the same side.
     * It is here for a reason the record layer cannot state for it: this artifact is
     * **render-ready**, which means a rasteriser must be able to produce the drawn shape
     * from the command alone -- so a `draw-border` that did not say *which* of the ten
     * styles it is would be a command its consumer could only guess at. A `draw-border`
     * therefore carries a style that generates a border, and a `draw-background` carries
     * `NONE`, because it has no side to style. `generates_border` rather than "not
     * `none`": a `hidden` border draws nothing, which is why RoboLayout gives such a side
     * a used width of zero and no command is built for it at all.
     */
    if (command->kind == ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER) {
        return robo_css_border_style_generates_border(command->style);
    }
    /*
     * And the text rule, the style rule's shape one field along and enforced from the same
     * side. A `draw-text` carries a **run** -- a non-empty span, a non-negative baseline
     * offset and font inputs that are a record -- because a command that draws characters
     * must say which ones and where the baseline is; every other drawing carries a cleared
     * one. Both halves are refusals: a `draw-text` with no characters is a drawing that
     * draws nothing, and a band that came with font inputs is a command carrying facts
     * about characters it does not have.
     */
    if (command->kind == ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT) {
        return command->style == ROBO_CSS_BORDER_STYLE_NONE &&
               robo_paint_text_run_is_valid(&command->text);
    }
    return command->style == ROBO_CSS_BORDER_STYLE_NONE &&
           !robo_paint_text_run_is_valid(&command->text);
}

int robo_paint_display_command_equal(const robo_paint_display_command_t *a,
                               const robo_paint_display_command_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->kind == b->kind && a->node == b->node && a->depth == b->depth &&
           robo_gfx_rect_equal(a->rect, b->rect) &&
           robo_paint_clip_equal(&a->clip, &b->clip) &&
           robo_css_color_equals(a->color, b->color) && a->style == b->style &&
           robo_paint_text_run_equal(&a->text, &b->text);
}

robo_paint_display_command_kind_t robo_paint_display_command_kind(const robo_paint_display_command_t *command)
{
    return command ? command->kind : ROBO_PAINT_DISPLAY_CMD_NONE;
}

robo_layout_node_id_t robo_paint_display_command_node(const robo_paint_display_command_t *command)
{
    return command ? command->node : ROBO_LAYOUT_NODE_ID_NONE;
}

robo_css_border_style_t robo_paint_display_command_style(const robo_paint_display_command_t *command)
{
    return command ? command->style : ROBO_CSS_BORDER_STYLE_NONE;
}

robo_paint_text_run_t robo_paint_display_command_text(const robo_paint_display_command_t *command)
{
    robo_paint_text_run_t run;

    if (!command) {
        robo_paint_text_run_init(&run);
        return run;
    }
    return command->text;
}

size_t robo_paint_display_command_depth(const robo_paint_display_command_t *command)
{
    return command ? command->depth : 0u;
}

robo_layout_rect_t robo_paint_display_command_rect(const robo_paint_display_command_t *command)
{
    return command ? command->rect : robo_gfx_rect(0, 0, 0, 0);
}

void robo_paint_display_command_clip(const robo_paint_display_command_t *command,
                              robo_paint_clip_t *out)
{
    if (!out) {
        return;
    }
    if (!command) {
        robo_paint_clip_init(out);
        return;
    }
    *out = command->clip;
}

robo_css_color_t robo_paint_display_command_color(const robo_paint_display_command_t *command)
{
    return command ? command->color : robo_css_color_none();
}

int robo_paint_display_command_is_structural(const robo_paint_display_command_t *command)
{
    return command != NULL && robo_paint_display_command_kind_is_structural(command->kind);
}

int robo_paint_display_command_is_marker(const robo_paint_display_command_t *command)
{
    return command != NULL && robo_paint_display_command_kind_is_marker(command->kind);
}

int robo_paint_display_command_is_drawing(const robo_paint_display_command_t *command)
{
    return command != NULL && robo_paint_display_command_kind_draws(command->kind);
}

robo_result_t robo_paint_display_command_write(const robo_paint_display_command_t *command,
                                        char *buffer,
                                        size_t capacity,
                                        size_t *out_length,
                                        robo_paint_diag_t *diag)
{
    robo_paint_writer_t text;
    const char *name;
    char color_text[ROBO_CSS_COLOR_TEXT_MAX + 1u];
    size_t color_length = 0u;
    size_t index;

    if (!command || !buffer || !out_length || capacity == 0u) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_INVALID_ARGUMENT,
                               "the command, a buffer and an out_length are required");
    }
    if (!robo_paint_display_command_kind_is_known(command->kind) ||
        command->kind == ROBO_PAINT_DISPLAY_CMD_NONE) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_INVALID_ARGUMENT,
                               "not a command kind");
    }

    robo_paint_writer_init(&text, buffer, capacity);
    name = robo_paint_display_command_kind_name(command->kind);
    robo_paint_writer_string(&text, name);
    for (index = strlen(name); index < ROBO_PAINT_DISPLAY_NAME_COLUMN; index++) {
        robo_paint_writer_char(&text, ' ');
    }

    robo_paint_writer_string(&text, "node=");
    robo_paint_writer_u64(&text, (uint64_t)command->node);
    robo_paint_writer_string(&text, " depth=");
    robo_paint_writer_u64(&text, (uint64_t)command->depth);

    /* The clip, as two modes with the interval appended only on an axis that has one:
     * `clip=hidden[0,100]/visible`. */
    robo_paint_writer_string(&text, " clip=");
    robo_paint_writer_string(&text, robo_css_overflow_name(command->clip.horizontal.mode));
    if (robo_paint_clip_axis_is_active(&command->clip.horizontal)) {
        robo_paint_writer_char(&text, '[');
        robo_paint_writer_i64(&text, (int64_t)command->clip.horizontal.start);
        robo_paint_writer_char(&text, ',');
        robo_paint_writer_i64(&text, (int64_t)command->clip.horizontal.end);
        robo_paint_writer_char(&text, ']');
    }
    robo_paint_writer_char(&text, '/');
    robo_paint_writer_string(&text, robo_css_overflow_name(command->clip.vertical.mode));
    if (robo_paint_clip_axis_is_active(&command->clip.vertical)) {
        robo_paint_writer_char(&text, '[');
        robo_paint_writer_i64(&text, (int64_t)command->clip.vertical.start);
        robo_paint_writer_char(&text, ',');
        robo_paint_writer_i64(&text, (int64_t)command->clip.vertical.end);
        robo_paint_writer_char(&text, ']');
    }

    /* The one rectangle the command acts on, printed the same way for every kind: the
     * whole point of a display command is that it carries no *other* rectangle to choose
     * between. */
    robo_paint_writer_string(&text, " rect=");
    robo_paint_writer_rect(&text, command->rect);

    if (robo_paint_display_command_kind_draws(command->kind)) {
        if (robo_css_color_write(command->color, color_text, sizeof(color_text),
                                 &color_length, NULL) == ROBO_OK) {
            robo_paint_writer_string(&text, " color=");
            robo_paint_writer_string(&text, color_text);
        }
    }
    /* A border band says which of the ten styles it is; nothing else has one to say. */
    if (command->kind == ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER) {
        robo_paint_writer_string(&text, " style=");
        robo_paint_writer_string(&text, robo_css_border_style_name(command->style));
    }
    /* A text command prints the span, the baseline and the font inputs; its characters
     * are printed by the list dump, which has the pool (see RoboPaintDump.h). */
    if (command->kind == ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT) {
        robo_paint_writer_string(&text, " code_points=");
        robo_paint_writer_u64(&text, (uint64_t)command->text.span.count);
        robo_paint_writer_string(&text, " baseline=");
        robo_paint_writer_i64(&text, (int64_t)command->text.baseline_offset);
        robo_paint_writer_string(&text, " font=");
        robo_paint_writer_number(&text, command->text.font.font_size_px, 2u);
        robo_paint_writer_char(&text, '/');
        robo_paint_writer_i64(&text, (int64_t)command->text.font.font_weight);
        robo_paint_writer_char(&text, '/');
        robo_paint_writer_string(&text, command->text.font.italic ? "italic" : "upright");
        robo_paint_writer_char(&text, '/');
        robo_paint_writer_string(&text,
                                 command->text.font.direction ==
                                         ROBO_LAYOUT_TEXT_DIRECTION_RTL ? "rtl" : "ltr");
    }

    *out_length = text.length;
    if (!robo_paint_writer_fits(&text)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_NOT_SUPPORTED,
                               "the command text does not fit the buffer");
    }
    return ROBO_OK;
}
