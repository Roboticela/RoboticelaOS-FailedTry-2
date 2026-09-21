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
    { ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, "push-clip" },
    { ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT, "draw-text" },
    { ROBO_PAINT_DISPLAY_CMD_POP_CLIP, "pop-clip" },
    { ROBO_PAINT_DISPLAY_CMD_END_NODE, "end-node" },
    /* 11.9's pair. Appended rather than inserted, for the reason `RoboPaintTypes.h` states at
     * the operation vocabulary: a stored enumerator is a contract, and the *paint* order these
     * two have -- an opacity scope opening before its box draws and closing before its end
     * marker -- is asserted by the structural check rather than encoded in a numbering. */
    { ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY, "push-opacity" },
    { ROBO_PAINT_DISPLAY_CMD_POP_OPACITY, "pop-opacity" },
    /* 11.7's drawing kind, appended for the same reason: a stored enumerator is a contract. Its
     * *paint* order -- between a node's border commands and its `CONTENT` -- is asserted by the
     * structural check, not encoded in a numbering. */
    { ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE, "draw-image" }
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
           kind == ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT ||
           kind == ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE;
}

int robo_paint_display_command_kind_uses_color(robo_paint_display_command_kind_t kind)
{
    /*
     * The kinds whose look **is** a colour, listed rather than written as "draws and is not an
     * image", so that the next payload kind is a kind that answers no here instead of a special
     * case in the validity rule -- the argument the operation vocabulary makes in the same shape.
     */
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

int robo_paint_display_command_kind_is_clip(robo_paint_display_command_kind_t kind)
{
    return kind == ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP ||
           kind == ROBO_PAINT_DISPLAY_CMD_POP_CLIP;
}

int robo_paint_display_command_kind_is_opacity(robo_paint_display_command_kind_t kind)
{
    return kind == ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY ||
           kind == ROBO_PAINT_DISPLAY_CMD_POP_OPACITY;
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
    /* Nothing in force, which is *fully opaque* -- the one field here a `memset` cannot clear
     * correctly, because zeroing it would be a valid, meaningful, fully transparent record. */
    robo_paint_opacity_init(&command->opacity);
    command->color = robo_css_color_none();
    command->style = ROBO_CSS_BORDER_STYLE_NONE;
    /* And no text run, which is what every kind except `draw-text` requires -- so "nobody
     * set it" and "this command draws no characters" are one state, as they are for the
     * style and the colour. */
    robo_paint_text_run_init(&command->text);
    /* And no image, which is what every kind except `draw-image` requires, for the third time
     * the same rule: a command that names no resource and a command that was never filled in
     * about one are one state rather than two that could disagree. */
    robo_paint_image_init(&command->image);
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
     * The **clip scope** rule (11.8), the record layer's rule one stage along: a command that
     * opens or closes a scope states that a boundary is in force, so a scope whose clip bounds
     * neither axis is a command whose fields disagree about what it is. Refused here rather
     * than emitted and skipped, and refused *before* the colour split so that the reason a
     * reader is given is the clip rather than the colour field a structural command leaves
     * cleared anyway.
     */
    if (robo_paint_display_command_kind_is_clip(command->kind) &&
        !robo_paint_clip_is_active(&command->clip)) {
        return 0;
    }
    /*
     * And the **opacity** rules (11.9), which are the clip rule's shape one field along plus the
     * range, and the range applies to **every** kind: an opacity outside `[0, 1]` is not a value a
     * rasteriser can apply, and a command carrying one would be a drawing whose strength its
     * consumer could not honour. A fully **opaque** scope command is refused for the same reason a
     * clip that bounds nothing is -- it states that a strength is in force and then states the
     * strength that changes nothing, which is a command a consumer could only skip.
     */
    if (!robo_paint_opacity_is_valid(&command->opacity)) {
        return 0;
    }
    if (robo_paint_display_command_kind_is_opacity(command->kind) &&
        robo_paint_opacity_is_opaque(&command->opacity)) {
        return 0;
    }
    /*
     * The colour rule, split on `uses_color` rather than on *drawing* or on *marker*. A command
     * whose look is a colour must carry one a rasteriser can use; every other command -- a
     * structural one, and since 11.7 a `draw-image` -- must carry `none`. The split is on
     * `uses_color` because `draw-image` **is** a drawing and has no colour to offer: a rule stated
     * on `draws` would be asking it for one, which is exactly the shape `RoboPaintTypes.h` argues
     * against. `CONTENT` lands on the no-colour side either way, which is the case the older
     * comment about `is_marker` was there to protect.
     */
    if (!robo_paint_display_command_kind_uses_color(command->kind)) {
        if (command->color.kind != ROBO_CSS_COLOR_NONE) {
            return 0;
        }
        /*
         * And inside that side, the two kinds part company: a `draw-image` must carry a payload
         * this build may draw and no style or run, and a structural command must carry none of
         * the four. The image half is the text rule's shape for a payload that is not
         * characters, and both halves are refusals for the same reason -- a `draw-image` with no
         * resource is a drawing that draws nothing, and a marker naming one is a command carrying
         * a drawing it does not make.
         */
        if (command->kind == ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE) {
            return robo_paint_image_is_valid(&command->image) &&
                   command->style == ROBO_CSS_BORDER_STYLE_NONE &&
                   !robo_paint_text_run_is_valid(&command->text);
        }
        return command->style == ROBO_CSS_BORDER_STYLE_NONE &&
               !robo_paint_text_run_is_valid(&command->text) &&
               robo_paint_image_is_cleared(&command->image);
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
        /*
         * And no image payload, which is the rule the structural branch states for the other
         * side of the split: a band that came with a resource would be a command carrying a
         * drawing it does not make. There is deliberately **no `draw-image` case above this
         * line** -- `draw-image` is not one of the kinds `uses_color` admits, so it was decided
         * in that branch, and a second check here would be a rule nothing could reach whose
         * disagreement with the first would be invisible.
         */
        return robo_css_border_style_generates_border(command->style) &&
               robo_paint_image_is_cleared(&command->image);
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
               robo_paint_text_run_is_valid(&command->text) &&
               robo_paint_image_is_cleared(&command->image);
    }
    return command->style == ROBO_CSS_BORDER_STYLE_NONE &&
           !robo_paint_text_run_is_valid(&command->text) &&
           robo_paint_image_is_cleared(&command->image);
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
           robo_paint_opacity_equal(&a->opacity, &b->opacity) &&
           robo_css_color_equals(a->color, b->color) && a->style == b->style &&
           robo_paint_text_run_equal(&a->text, &b->text) &&
           /* The image payload compares by **identity** rather than by address, which is the
            * reason `robo_paint_image_equal` is a door instead of four more comparisons here:
            * two lists built from one document must compare equal whatever the heap did. */
           robo_paint_image_equal(&a->image, &b->image);
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

robo_paint_image_t robo_paint_display_command_image(const robo_paint_display_command_t *command)
{
    robo_paint_image_t image;

    robo_paint_image_init(&image);
    if (!command || command->kind != ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE) {
        /* The record layer's rule one artifact along: a kind that is not the kind that carries a
         * payload answers with a cleared one rather than with the field. */
        return image;
    }
    return command->image;
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

void robo_paint_display_command_opacity(const robo_paint_display_command_t *command,
                                       robo_paint_opacity_t *out)
{
    if (!out) {
        return;
    }
    if (!command) {
        /* Nothing in force, which is full strength -- the same answer an empty scope stack
         * gives. */
        robo_paint_opacity_init(out);
        return;
    }
    *out = command->opacity;
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

int robo_paint_display_command_is_clip(const robo_paint_display_command_t *command)
{
    return command != NULL && robo_paint_display_command_kind_is_clip(command->kind);
}

int robo_paint_display_command_is_opacity(const robo_paint_display_command_t *command)
{
    return command != NULL && robo_paint_display_command_kind_is_opacity(command->kind);
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

    /* The **opacity in force** (11.9), beside the clip and for the reason the clip is beside
     * it: a command that produces visible pixels answers `how strongly` as well as `where`,
     * and a dump that omitted it would leave the one number a reader needs to explain a
     * faint rectangle the one number it does not show. */
    robo_paint_writer_string(&text, " opacity=");
    robo_paint_writer_number(&text, robo_paint_opacity_value(&command->opacity), 2u);

    /* The one rectangle the command acts on, printed the same way for every kind: the
     * whole point of a display command is that it carries no *other* rectangle to choose
     * between. */
    robo_paint_writer_string(&text, " rect=");
    robo_paint_writer_rect(&text, command->rect);

    /*
     * The colour, for the kinds that **have** one rather than for the kinds that draw, which
     * 11.7 is the phase that separates: a `draw-image` is a drawing whose look is its resource's
     * pixels, so a line that asked it for a colour would print nothing or a "none" a reader would
     * take for a fill.
     */
    if (robo_paint_display_command_kind_uses_color(command->kind)) {
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
    /*
     * An image command prints the **resource's identity**, the source rectangle it samples and
     * the resource's own dimensions -- the same facts the record's writer prints, and no address,
     * for the same reason: two runs of one document must produce the same lines. The pixels are
     * not printed: a command names them, and naming them is what a reader diffs.
     */
    if (command->kind == ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE) {
        robo_paint_writer_string(&text, " resource=");
        robo_paint_writer_u64(&text, (uint64_t)robo_paint_image_identity(&command->image));
        robo_paint_writer_string(&text, " src=");
        robo_paint_writer_i64(&text, (int64_t)command->image.source.x);
        robo_paint_writer_char(&text, ',');
        robo_paint_writer_i64(&text, (int64_t)command->image.source.y);
        robo_paint_writer_char(&text, ',');
        robo_paint_writer_i64(&text, (int64_t)command->image.source.width);
        robo_paint_writer_char(&text, 'x');
        robo_paint_writer_i64(&text, (int64_t)command->image.source.height);
        robo_paint_writer_string(&text, " source_size=");
        robo_paint_writer_u64(&text, (uint64_t)command->image.width);
        robo_paint_writer_char(&text, 'x');
        robo_paint_writer_u64(&text, (uint64_t)command->image.height);
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
