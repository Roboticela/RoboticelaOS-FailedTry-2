/*
 * RoboPaint operations (Phase 11.1–11.2) — see RoboPaintOperation.h.
 *
 * The record, the validity rule that decides whether it may enter a list, the two
 * coordinate spaces in one call, and the band a border side occupies. The validity rule
 * is the one piece of policy here and it lives in one function so that
 * `robo_paint_list_append` and every read door share it rather than each deciding what
 * "a record" means.
 */

#include "RoboPaint/RoboPaintOperation.h"

#include "RoboPaintInternal.h"

#include <string.h>

/** Spaces the kind name is padded to, so that a two-list diff lines up. */
#define ROBO_PAINT_OP_NAME_COLUMN 14u

void robo_paint_operation_init(robo_paint_operation_t *operation)
{
    if (!operation) {
        return;
    }
    memset(operation, 0, sizeof(*operation));
    operation->kind = ROBO_PAINT_OP_NONE;
    operation->node = ROBO_LAYOUT_NODE_ID_NONE;
    robo_paint_boxes_init(&operation->boxes);
    operation->origin = robo_gfx_point(0, 0);
    robo_paint_clip_init(&operation->clip);
    /* And nothing in force, which is *fully opaque* rather than zero -- the one field here
     * whose cleared state a `memset` does not produce, and the reason it is written by a
     * door. A zeroed opacity record is a valid, meaningful, fully transparent one, so a
     * record nothing set would be a drawing at zero strength rather than a drawing at full
     * strength (RoboPaintOpacity.h). */
    robo_paint_opacity_init(&operation->opacity);
    operation->color = robo_css_color_none();
    /* A cleared record carries no border style, which is what a non-border kind requires
     * of every record -- so "nobody set it" and "this is not a border" are one state. */
    operation->style = ROBO_CSS_BORDER_STYLE_NONE;
    /* And no text run, which is what a non-text kind requires: an empty span, no baseline
     * and the CSS initial font inputs, so "nobody set it" and "this is not text" are one
     * state the same way. The memset above already zeroed the storage; this is here so
     * that "a cleared run" is `RoboPaintText.h`'s definition rather than a zero pattern
     * this file happens to agree with. */
    robo_paint_text_run_init(&operation->text);
    /* And no image payload, which is what a non-image kind requires of every record, for the
     * same reason: "nobody set it" and "this drawing has no resource" are one state. */
    robo_paint_image_init(&operation->image);
}

int robo_paint_operation_is_valid(const robo_paint_operation_t *operation)
{
    if (!operation) {
        return 0;
    }
    if (!robo_paint_op_kind_is_known(operation->kind)) {
        return 0;
    }
    if (operation->kind == ROBO_PAINT_OP_NONE) {
        /* A cleared record is a state, not an operation. Refusing it here is what keeps
         * "the list is empty" and "the list has one meaningless entry" apart. */
        return 0;
    }
    if (operation->node == ROBO_LAYOUT_NODE_ID_NONE) {
        return 0;
    }
    if (!robo_paint_boxes_is_valid(&operation->boxes)) {
        return 0;
    }
    if (!robo_paint_clip_is_valid(&operation->clip)) {
        return 0;
    }
    /*
     * The **clip scope** rule (11.8), and it is the "this record's fields agree about what it
     * is" rule for a third time. A `PUSH_CLIP`/`POP_CLIP` record states that a boundary is in
     * force; a scope whose clip bounds neither axis states the opposite, and a consumer that
     * honoured it would push and pop a record that bounds nothing -- so it is refused here
     * rather than emitted and skipped by every reader.
     *
     * It is checked *before* the colour split rather than inside the structural branch,
     * because the fact it is about is the clip and not the colour: a clip record with a colour
     * is refused by the structural branch below, and this rule would still be the right answer
     * for one that reached here.
     */
    if (robo_paint_op_kind_is_clip(operation->kind) &&
        !robo_paint_clip_is_active(&operation->clip)) {
        return 0;
    }
    /*
     * The **opacity scope** rule (11.9), the clip rule's sibling one field along, and it is the
     * same shape for the same reason: a `PUSH_OPACITY`/`POP_OPACITY` record states that a
     * strength is in force, so a scope that is fully **opaque** states the opposite -- a
     * consumer that honoured it would push and pop a record that changes nothing about what is
     * inside it, which is a record a consumer could only skip.
     *
     * The range is checked for **every** kind rather than only for the two scopes, and that is
     * the one place this rule is wider than the clip's: the clip is a well-formed value on every
     * record whatever it bounds, while an opacity outside `[0, 1]` is not a value at all -- so a
     * drawing carrying `2` would be a drawing whose strength a rasteriser cannot apply. A
     * structural record is held to the same rule, because "what strength was in force here" is
     * a question every record answers.
     */
    if (!robo_paint_opacity_is_valid(&operation->opacity)) {
        return 0;
    }
    if (robo_paint_op_kind_is_opacity(operation->kind) &&
        robo_paint_opacity_is_opaque(&operation->opacity)) {
        return 0;
    }
    /*
     * The colour rule, and it is two rules because the two halves of the vocabulary have
     * different answers. A kind whose appearance **is** a colour -- the background, the four
     * border sides and a text run -- must carry a colour a rasteriser can use: resolved, or
     * `currentColor` that this layer could not resolve (`robo_css_color_is_valid` is exactly
     * that pair). `none` on such a kind is refused because a fill or a border with no colour
     * draws nothing, and an operation that draws nothing is not a statement about the
     * document. Every other kind must carry the colour that says nothing was declared --
     * `robo_css_color_none()`, which is RoboCSS's own "no colour", not a black a consumer
     * might draw.
     *
     * The split is `robo_paint_op_kind_uses_color` rather than `draws`, and 11.7 is why the
     * two arrived a phase apart: `draws` is true of an `IMAGE` record, whose appearance is its
     * resource's pixels, so a rule stated on `draws` would be asking an image for a colour it
     * cannot have. Stated positively -- "these kinds have a colour" -- the next payload kind
     * says so in one place instead of being added to an exception list here. It is not
     * `is_marker` either, so that `CONTENT`, a structural record that is not a box marker, lands
     * on the "carries no colour" side rather than being asked for one.
     */
    if (robo_paint_op_kind_uses_color(operation->kind)) {
        if (!robo_css_color_is_valid(operation->color)) {
            return 0;
        }
        /*
         * The style rule, and it is the colour rule's shape one field along. A border
         * must carry a style that **generates** a border, and the reason it is
         * `generates_border` rather than "not `none`" is 10.11's own rule: `none` and
         * `hidden` both draw nothing, which is why such a side's *used* width is zero and
         * no record is emitted for it at all. So a border record whose style draws
         * nothing is a record whose fields disagree about what it is, and it is refused
         * here rather than drawn as nothing by a consumer that trusted the kind.
         *
         * A **background** must carry `NONE`: it has no side style, and a background with
         * one would be carrying a fact about a border it is not.
         *
         * And the **text rule** (11.6), the same shape a third time: a `TEXT` record must
         * carry a run -- a non-empty span, a non-negative baseline offset and font inputs
         * that are a record -- and a kind that is not text must carry a **cleared** one.
         * Both halves are refusals: a text record with no characters is a drawing that
         * draws nothing, and a background that came with font inputs is a record carrying
         * facts about characters it does not have. Nothing here checks the span against a
         * pool, because that is a question about a container and this door takes a
         * record (`RoboPaintList.h` asks it where the pool is).
         */
        if (operation->kind == ROBO_PAINT_OP_TEXT) {
            return operation->style == ROBO_CSS_BORDER_STYLE_NONE &&
                   robo_paint_text_run_is_valid(&operation->text) &&
                   robo_paint_image_is_cleared(&operation->image);
        }
        if (robo_paint_op_kind_is_border(operation->kind)) {
            return robo_css_border_style_generates_border(operation->style);
        }
        return operation->style == ROBO_CSS_BORDER_STYLE_NONE &&
               !robo_paint_text_run_is_valid(&operation->text) &&
               robo_paint_image_is_cleared(&operation->image);
    }
    /*
     * Every kind whose appearance is **not** a colour this module resolved -- a structural
     * record and, since 11.7, an `IMAGE` record -- and the first half of the rule is the same
     * for both: the colour field must be `none`, because a colour on a record that has no fill
     * is a fact about a drawing that does not exist. The colour rule above is stated on
     * `uses_color` rather than on `draws` for exactly this reason, and this is the branch that
     * makes the split worth having.
     *
     * A **structural** record draws nothing, so it carries the absence of a colour, of a style
     * and of a text run -- and of an image payload too: a `BEGIN_BOX` naming a resource would
     * be a marker carrying a drawing. The text half is stated as "the run is not a run" rather
     * than as a field-by-field comparison against a cleared record, because a cleared run is
     * exactly a run `robo_paint_text_run_is_valid` refuses and a second spelling of it would be
     * a second place for the two to drift.
     *
     * And the **image rule** (11.7), the first of these whose subject is not a colour: an
     * `IMAGE` record's appearance **is** its resource's pixels, so it must carry a payload this
     * build may draw -- a resource with a surface, positive intrinsic dimensions and a source
     * rectangle inside it -- and, carrying no colour, it must carry no **style** either. Its
     * text run must be cleared for the reason every non-`TEXT` kind's must: font inputs on a
     * record that draws no glyphs are facts about characters it does not have. The two halves
     * are the same "its fields agree about what it is" rule the colour split states, applied to
     * a payload instead of to a fill.
     */
    if (operation->color.kind != ROBO_CSS_COLOR_NONE) {
        return 0;
    }
    if (operation->kind == ROBO_PAINT_OP_IMAGE) {
        return robo_paint_image_is_valid(&operation->image) &&
               operation->style == ROBO_CSS_BORDER_STYLE_NONE &&
               !robo_paint_text_run_is_valid(&operation->text);
    }
    return operation->style == ROBO_CSS_BORDER_STYLE_NONE &&
           !robo_paint_text_run_is_valid(&operation->text) &&
           robo_paint_image_is_cleared(&operation->image);
}

int robo_paint_operation_equal(const robo_paint_operation_t *a,
                               const robo_paint_operation_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->kind == b->kind && a->node == b->node && a->depth == b->depth &&
           robo_paint_boxes_equal(&a->boxes, &b->boxes) &&
           robo_gfx_point_equal(a->origin, b->origin) &&
           robo_paint_clip_equal(&a->clip, &b->clip) &&
           robo_paint_opacity_equal(&a->opacity, &b->opacity) &&
           robo_css_color_equals(a->color, b->color) && a->style == b->style &&
           robo_paint_text_run_equal(&a->text, &b->text) &&
           /* An image payload compares by **identity**, not by address, which is why
            * `robo_paint_image_equal` exists as a door rather than this being four more
            * comparisons here (RoboPaintImage.h). */
           robo_paint_image_equal(&a->image, &b->image);
}

robo_paint_op_kind_t robo_paint_operation_kind(const robo_paint_operation_t *operation)
{
    return operation ? operation->kind : ROBO_PAINT_OP_NONE;
}

robo_layout_node_id_t robo_paint_operation_node(const robo_paint_operation_t *operation)
{
    return operation ? operation->node : ROBO_LAYOUT_NODE_ID_NONE;
}

size_t robo_paint_operation_depth(const robo_paint_operation_t *operation)
{
    return operation ? operation->depth : 0u;
}

void robo_paint_operation_boxes(const robo_paint_operation_t *operation,
                                robo_paint_boxes_t *out)
{
    if (!out) {
        return;
    }
    if (!operation) {
        robo_paint_boxes_init(out);
        return;
    }
    *out = operation->boxes;
}

void robo_paint_operation_origin(const robo_paint_operation_t *operation,
                                 robo_layout_point_t *out)
{
    if (!out) {
        return;
    }
    *out = operation ? operation->origin : robo_gfx_point(0, 0);
}

void robo_paint_operation_clip(const robo_paint_operation_t *operation,
                               robo_paint_clip_t *out)
{
    if (!out) {
        return;
    }
    if (!operation) {
        robo_paint_clip_init(out);
        return;
    }
    *out = operation->clip;
}

void robo_paint_operation_opacity(const robo_paint_operation_t *operation,
                                 robo_paint_opacity_t *out)
{
    if (!out) {
        return;
    }
    if (!operation) {
        /* Nothing in force, which is full strength -- the same answer an empty scope stack
         * gives, so the two states a caller can be in read alike. */
        robo_paint_opacity_init(out);
        return;
    }
    *out = operation->opacity;
}

robo_css_color_t robo_paint_operation_color(const robo_paint_operation_t *operation)
{
    return operation ? operation->color : robo_css_color_none();
}

robo_css_border_style_t robo_paint_operation_style(const robo_paint_operation_t *operation)
{
    return operation ? operation->style : ROBO_CSS_BORDER_STYLE_NONE;
}

robo_paint_text_run_t robo_paint_operation_text(const robo_paint_operation_t *operation)
{
    robo_paint_text_run_t run;

    if (!operation) {
        robo_paint_text_run_init(&run);
        return run;
    }
    return operation->text;
}

robo_paint_image_t robo_paint_operation_image(const robo_paint_operation_t *operation)
{
    robo_paint_image_t image;

    robo_paint_image_init(&image);
    if (!operation || operation->kind != ROBO_PAINT_OP_IMAGE) {
        /* A kind that is not an image answers with a cleared payload rather than with the
         * field, which is the rule the text accessor states one field along: a caller that
         * did not ask the kind gets "no resource", not a leftover. */
        return image;
    }
    return operation->image;
}

int robo_paint_operation_is_structural(const robo_paint_operation_t *operation)
{
    return operation != NULL && robo_paint_op_kind_is_structural(operation->kind);
}

int robo_paint_operation_is_marker(const robo_paint_operation_t *operation)
{
    return operation != NULL && robo_paint_op_kind_is_marker(operation->kind);
}

int robo_paint_operation_is_drawing(const robo_paint_operation_t *operation)
{
    return operation != NULL && robo_paint_op_kind_draws(operation->kind);
}

robo_result_t robo_paint_operation_side(const robo_paint_operation_t *operation,
                                        robo_css_box_side_t *out)
{
    if (!operation || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_paint_op_kind_side(operation->kind, out);
}

robo_result_t robo_paint_operation_local_boxes(const robo_paint_operation_t *operation,
                                               robo_paint_boxes_t *out,
                                               robo_paint_diag_t *diag)
{
    if (!operation || !out) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_INVALID_ARGUMENT,
                               "the operation and an output are required");
    }
    if (!robo_paint_boxes_is_valid(&operation->boxes)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_ERROR,
                               "the operation's boxes are not a record");
    }
    return robo_paint_boxes_translate(&operation->boxes, -operation->origin.x,
                                      -operation->origin.y, out, diag);
}

robo_result_t robo_paint_operation_side_band(const robo_paint_operation_t *operation,
                                             robo_layout_rect_t *out,
                                             robo_paint_diag_t *diag)
{
    robo_css_box_side_t side;
    const robo_layout_rect_t *border;
    const robo_layout_rect_t *padding;
    int32_t border_right;
    int32_t border_bottom;
    int32_t padding_right;
    int32_t padding_bottom;

    if (!operation || !out) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_INVALID_ARGUMENT,
                               "the operation and an output are required");
    }
    if (robo_paint_op_kind_side(operation->kind, &side) != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_INVALID_ARGUMENT,
                               "only a border operation occupies a band");
    }
    border = &operation->boxes.border;
    padding = &operation->boxes.padding;
    border_right = robo_paint_add_saturated(border->x, border->width);
    border_bottom = robo_paint_add_saturated(border->y, border->height);
    padding_right = robo_paint_add_saturated(padding->x, padding->width);
    padding_bottom = robo_paint_add_saturated(padding->y, padding->height);

    /*
     * The band on one side is the region of the border box outside the padding box on
     * that edge, and the two axes are answered differently on purpose:
     *
     *   a **top** or **bottom** band spans the border box's full width and stops where
     *   the padding box begins or ends vertically;
     *   a **left** or **right** band spans the border box's full height and stops where
     *   the padding box begins or ends horizontally.
     *
     * Both faces of an axis contribute to the *other* axis's extent, which is why the
     * full width (or height) is used rather than the difference on both axes: a box with
     * a left border and a right border has two bands, each as tall as the box.
     *
     * Every extent is floored at zero, so a box whose insets were saturated or declined
     * produces an empty band rather than a negative one -- and an empty band is a
     * correct answer for a border that occupies no room.
     */
    switch (side) {
    case ROBO_CSS_BOX_SIDE_TOP:
        out->x = border->x;
        out->y = border->y;
        out->width = border->width;
        out->height = robo_paint_extent((int64_t)padding->y - (int64_t)border->y);
        break;
    case ROBO_CSS_BOX_SIDE_RIGHT:
        out->x = padding_right;
        out->y = border->y;
        out->width = robo_paint_extent((int64_t)border_right - (int64_t)padding_right);
        out->height = border->height;
        break;
    case ROBO_CSS_BOX_SIDE_BOTTOM:
        out->x = border->x;
        out->y = padding_bottom;
        out->width = border->width;
        out->height = robo_paint_extent((int64_t)border_bottom - (int64_t)padding_bottom);
        break;
    case ROBO_CSS_BOX_SIDE_LEFT:
        out->x = border->x;
        out->y = border->y;
        out->width = robo_paint_extent((int64_t)padding->x - (int64_t)border->x);
        out->height = border->height;
        break;
    default:
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_INVALID_ARGUMENT,
                               "not a border side");
    }
    return ROBO_OK;
}

robo_result_t robo_paint_operation_write(const robo_paint_operation_t *operation,
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
    robo_layout_rect_t rect;
    int have_rect = 0;

    if (!operation || !buffer || !out_length || capacity == 0u) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_INVALID_ARGUMENT,
                               "the operation, a buffer and an out_length are required");
    }
    if (!robo_paint_op_kind_is_known(operation->kind) ||
        operation->kind == ROBO_PAINT_OP_NONE) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_INVALID_ARGUMENT,
                               "not an operation kind");
    }

    robo_paint_writer_init(&text, buffer, capacity);
    name = robo_paint_op_kind_name(operation->kind);
    robo_paint_writer_string(&text, name);
    for (index = strlen(name); index < ROBO_PAINT_OP_NAME_COLUMN; index++) {
        robo_paint_writer_char(&text, ' ');
    }

    robo_paint_writer_string(&text, "node=");
    robo_paint_writer_u64(&text, (uint64_t)operation->node);
    robo_paint_writer_string(&text, " depth=");
    robo_paint_writer_u64(&text, (uint64_t)operation->depth);

    /* The clip, as two modes with the interval appended only on an axis that has one:
     * `clip=hidden[0,100]/visible`. */
    robo_paint_writer_string(&text, " clip=");
    robo_paint_writer_string(&text, robo_css_overflow_name(operation->clip.horizontal.mode));
    if (robo_paint_clip_axis_is_active(&operation->clip.horizontal)) {
        robo_paint_writer_char(&text, '[');
        robo_paint_writer_i64(&text, (int64_t)operation->clip.horizontal.start);
        robo_paint_writer_char(&text, ',');
        robo_paint_writer_i64(&text, (int64_t)operation->clip.horizontal.end);
        robo_paint_writer_char(&text, ']');
    }
    robo_paint_writer_char(&text, '/');
    robo_paint_writer_string(&text, robo_css_overflow_name(operation->clip.vertical.mode));
    if (robo_paint_clip_axis_is_active(&operation->clip.vertical)) {
        robo_paint_writer_char(&text, '[');
        robo_paint_writer_i64(&text, (int64_t)operation->clip.vertical.start);
        robo_paint_writer_char(&text, ',');
        robo_paint_writer_i64(&text, (int64_t)operation->clip.vertical.end);
        robo_paint_writer_char(&text, ']');
    }

    /*
     * The **opacity in force** (11.9), printed for every kind exactly as the clip is, and the
     * reason is the same one: what is in force is a question every record answers, so a field
     * that appeared only on a scope would make a drawing's strength unreadable from the drawing
     * a dump is being read for. Two decimals, because a record's value is in `[0, 1]` and two
     * are enough to tell every value a document is likely to declare from its neighbours --
     * `opacity=0.50` beside `opacity=1.00`. The writer is this module's own, so the spelling is
     * deterministic and locale-independent rather than `printf`'s.
     */
    robo_paint_writer_string(&text, " opacity=");
    robo_paint_writer_number(&text, robo_paint_opacity_value(&operation->opacity), 2u);

    /* The rectangle this kind acts on: a marker and a background act on the border box,
     * a border acts on its band. Which one is printed is the kind's business, so a reader
     * of a dump does not have to remember that `background` means "the border box". */
    if (robo_paint_op_kind_is_border(operation->kind)) {
        if (robo_paint_operation_side_band(operation, &rect, NULL) == ROBO_OK) {
            have_rect = 1;
        }
        robo_paint_writer_string(&text, " band=");
    } else if (operation->kind == ROBO_PAINT_OP_BACKGROUND) {
        rect = operation->boxes.border;
        have_rect = 1;
        robo_paint_writer_string(&text, " rect=");
    } else if (operation->kind == ROBO_PAINT_OP_CONTENT) {
        /* The content boundary acts on the inner rectangle: the region a box's own
         * painting ends at and its contents begin in. Printed explicitly so a reader of
         * a dump sees which of the three rectangles the record is about, rather than
         * having to remember that `content` means "the content box". */
        rect = operation->boxes.content;
        have_rect = 1;
        robo_paint_writer_string(&text, " content=");
    } else if (robo_paint_op_kind_is_clip(operation->kind)) {
        /*
         * A **clip scope** acts on the padding box -- the boundary the box's own `overflow`
         * resolved to -- which is the same rectangle the display command carries, and the
         * reason this is a branch at all is that the generic case below would print the
         * *border* box and tell a reader the scope came from a rectangle it did not come
         * from. The boundary itself is in the `clip` field above; this is the box it
         * belongs to.
         */
        rect = operation->boxes.padding;
        have_rect = 1;
        robo_paint_writer_string(&text, " padding=");
    } else {
        rect = operation->boxes.border;
        have_rect = 1;
        robo_paint_writer_string(&text, " border=");
    }
    if (have_rect) {
        robo_paint_writer_rect(&text, rect);
    }

    /*
     * The colour, printed for the kinds that **have** one (`robo_paint_op_kind_uses_color`)
     * rather than for the kinds that draw. 11.7 is why the two are not the same question: an
     * `IMAGE` record draws and carries `none`, so a line that asked it for a colour would print
     * either nothing or a "none" that a reader would take for a fill.
     */
    if (robo_paint_op_kind_uses_color(operation->kind)) {
        if (robo_css_color_write(operation->color, color_text, sizeof(color_text),
                                 &color_length, NULL) == ROBO_OK) {
            robo_paint_writer_string(&text, " color=");
            robo_paint_writer_string(&text, color_text);
        }
    }
    /* Printed exactly where it means something, which is how the record states it too: a
     * border says which of the ten styles it draws, and nothing else has one to print. */
    if (robo_paint_op_kind_is_border(operation->kind)) {
        robo_paint_writer_string(&text, " style=");
        robo_paint_writer_string(&text, robo_css_border_style_name(operation->style));
    }
    /*
     * A text record prints the **span** and not the characters, and the difference is a
     * lifetime rather than a preference: this door is handed one record, and a record
     * carries two numbers that name characters in a pool it does not have. The characters
     * are printed by the two list dumps (`RoboPaintDump.h`), which do have the pool --
     * which is also where a reader wants them, because that is the view of a whole
     * document rather than of one line of it.
     */
    /*
     * An image record prints the **resource's identity**, the source rectangle and the
     * resource's own dimensions -- everything the record carries, and **no address**: an
     * identity is a fact about the drawing, where a pointer is a fact about this process's
     * heap, and a dump whose lines changed between two runs of one document would be a dump
     * nobody could diff (`RoboPaintImage.h`). The pixels are not printed here for the same
     * reason a run's characters are not: a record names them, and this door has no pool of
     * them to read.
     */
    if (operation->kind == ROBO_PAINT_OP_IMAGE) {
        robo_paint_writer_string(&text, " resource=");
        robo_paint_writer_u64(&text, (uint64_t)robo_paint_image_identity(&operation->image));
        /*
         * The source rectangle is a **RoboGraphics** rectangle rather than a layout one -- it
         * is in image pixels, which is the only space an image's own pixels have -- so it is
         * written field by field here instead of through the layout-rectangle helper. The
         * order is that helper's so the two read alike in a dump: `x,y,wxh`.
         */
        robo_paint_writer_string(&text, " src=");
        robo_paint_writer_i64(&text, (int64_t)operation->image.source.x);
        robo_paint_writer_char(&text, ',');
        robo_paint_writer_i64(&text, (int64_t)operation->image.source.y);
        robo_paint_writer_char(&text, ',');
        robo_paint_writer_i64(&text, (int64_t)operation->image.source.width);
        robo_paint_writer_char(&text, 'x');
        robo_paint_writer_i64(&text, (int64_t)operation->image.source.height);
        robo_paint_writer_string(&text, " source_size=");
        robo_paint_writer_u64(&text, (uint64_t)operation->image.width);
        robo_paint_writer_char(&text, 'x');
        robo_paint_writer_u64(&text, (uint64_t)operation->image.height);
    }
    if (operation->kind == ROBO_PAINT_OP_TEXT) {
        robo_paint_writer_string(&text, " code_points=");
        robo_paint_writer_u64(&text, (uint64_t)operation->text.span.count);
        robo_paint_writer_string(&text, " baseline=");
        robo_paint_writer_i64(&text, (int64_t)operation->text.baseline_offset);
        robo_paint_writer_string(&text, " font=");
        robo_paint_writer_number(&text, operation->text.font.font_size_px, 2u);
        robo_paint_writer_char(&text, '/');
        robo_paint_writer_i64(&text, (int64_t)operation->text.font.font_weight);
        robo_paint_writer_char(&text, '/');
        robo_paint_writer_string(&text, operation->text.font.italic ? "italic" : "upright");
        robo_paint_writer_char(&text, '/');
        robo_paint_writer_string(&text,
                                 operation->text.font.direction ==
                                         ROBO_LAYOUT_TEXT_DIRECTION_RTL ? "rtl" : "ltr");
    }

    *out_length = text.length;
    if (!robo_paint_writer_fits(&text)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_NOT_SUPPORTED,
                               "the operation text does not fit the buffer");
    }
    return ROBO_OK;
}
