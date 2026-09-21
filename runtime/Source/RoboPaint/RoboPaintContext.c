/*
 * RoboPaint context and the deterministic traversal (Phase 11.1–11.2) — see
 * RoboPaintContext.h.
 *
 * This file is the pass: the walk, the accumulation and the stats. Everything it
 * concludes comes from RoboLayout's own read doors -- there is no width, height, margin,
 * padding, border, position or overflow arithmetic here beyond the two things layout
 * cannot know, the page-space accumulation and the per-axis clip intersection, and both
 * of those live in RoboPaintGeometry.c so that this file stays a traversal.
 *
 * The walk is deliberately the dumbest correct thing: depth-first, document order, no
 * stack of its own beyond the call stack, and one operation per fact the box states. The
 * three properties the phase claims are consequences of that shape rather than checks
 * bolted onto it:
 *
 *   **determinism** -- the order is the tree's (`first_child` then `next_sibling`) and
 *   every number is a bounded record's, so two passes over one tree produce byte-identical
 *   lists (`robo_paint_list_equal`);
 *
 *   **purity** -- every door on the left-hand side takes the tree by `const` pointer, so
 *   "the painter does not write layout" is the signature;
 *
 *   **no re-derivation** -- the boxes come from `robo_paint_boxes_of`, the colours from
 *   the box's own style and its *used* border record, and the clip from the overflow the
 *   layout pass measured. Paint decides only *whether* something draws, which is the
 *   question layout does not ask.
 */

#include "RoboPaint/RoboPaintContext.h"

#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboCSS/RoboCSSValue.h"

#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTag.h"

#include "RoboLayout/RoboLayoutBaseline.h"

#include "RoboPaint/RoboPaintGeometry.h"
#include "RoboPaint/RoboPaintImage.h"
#include "RoboPaint/RoboPaintOpacity.h"
#include "RoboPaint/RoboPaintOperation.h"
#include "RoboPaint/RoboPaintText.h"

/* The three element-level style reads this file defines are shared with 11.10's hit-test
 * walk (`RoboPaintInternal.h` argues why they are privately linked rather than installed),
 * so the header is included here to check the definitions against their declarations. */
#include "RoboPaintInternal.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"

#include <string.h>

struct robo_paint_context {
    robo_paint_stats_t stats;
    robo_paint_pass_outcome_t outcome;
    /* Borrowed and const as read back: the pass read the tree, it did not write it. */
    const robo_layout_tree_t *tree;
    void *user_data;
};

/* ------------------------------------------------------------------ */
/* The pass outcome                                                    */
/* ------------------------------------------------------------------ */

int robo_paint_pass_outcome_is_known(robo_paint_pass_outcome_t outcome)
{
    return outcome >= ROBO_PAINT_PASS_NONE &&
           outcome < (robo_paint_pass_outcome_t)ROBO_PAINT_PASS_OUTCOME_COUNT;
}

const char *robo_paint_pass_outcome_name(robo_paint_pass_outcome_t outcome)
{
    switch (outcome) {
    case ROBO_PAINT_PASS_NONE:
        return "none";
    case ROBO_PAINT_PASS_PAINTED:
        return "painted";
    case ROBO_PAINT_PASS_FAILED:
        return "failed";
    case ROBO_PAINT_PASS_STALE:
        return "stale";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Reading a colour out of a computed style                            */
/* ------------------------------------------------------------------ */

/*
 * The one place this module reads a property RoboLayout deliberately ignored, and it is
 * the *paint-visible* half of 10.14's classification rather than a boundary violation:
 * `background-color` and the border colours are the properties 10.14 says layout does
 * not read and paint does. The read is by **name** through
 * `robo_css_computed_style_computed_by_name`, which is the same door
 * `RoboLayoutBorder.c` uses for the same reason: no property id has to have been
 * resolved for the property to be found.
 */

/** The `COLOR` value of @p property in @p style, or a `NONE` colour. */
robo_css_color_t robo_paint_property_color(const robo_css_computed_style_t *style,
                                          const char *property)
{
    const robo_css_computed_value_t *value;

    if (!style || !property) {
        return robo_css_color_none();
    }
    value = robo_css_computed_style_computed_by_name(style, robo_css_slice_from_text(property));
    if (value && value->kind == ROBO_CSS_COMPUTED_VALUE_COLOR) {
        return robo_css_computed_value_color(value);
    }
    /* Absent, or present as something that is not a colour. Both answer "no colour was
     * declared", which is the statement a caller of this helper can act on. */
    return robo_css_color_none();
}

/** The element's own computed `color`, or a `NONE` colour. */
robo_css_color_t robo_paint_element_color(const robo_css_computed_style_t *style)
{
    return robo_paint_property_color(style, "color");
}

/* ------------------------------------------------------------------ */
/* Reading the opacity out of a computed style (11.9)                  */
/* ------------------------------------------------------------------ */

/*
 * The second property this module reads that RoboLayout deliberately ignores, read through
 * the same door and for the same reason `background-color` is: `opacity` is paint-visible
 * and layout-invisible, so it is 10.14's `STYLE` bit without a layout bit and this layer is
 * where it is consumed.
 *
 * Three rules are worth stating here, because each one is a trap:
 *
 *   **the kind is checked, not the magnitude**   the value arrives as
 *   `ROBO_CSS_COMPUTED_VALUE_OPACITY` and only that kind is read. A `200px` width is a
 *   *number* too, so a reader that took the real out of whatever it found would read a
 *   width as an opacity of `200` -- `RoboCSSComputedValue.h` gives opacity its own kind for
 *   exactly this reason, and this is the reader that would have made it necessary.
 *
 *   **absent and present-and-one are different answers, and both mean opaque**   no
 *   declaration and `opacity: 1` reach the same drawing, and
 *   `robo_css_computed_value_opacity` is the door that answers *one* for a value that is not
 *   an opacity rather than zero -- because zero would make every element of every document
 *   that never mentions `opacity` invisible.
 *
 *   **it does not inherit, and the run is where that matters**   this function is called for
 *   **boxes only**. A text run's style is its parent element's, borrowed (`RoboLayoutText.h`),
 *   and that element has already opened an opacity scope around it -- so a run that read its
 *   own style's opacity would apply the same declaration a second time and square it.
 */

/** The opacity @p style declares, or the fully opaque record when it declares none. */
robo_paint_opacity_t robo_paint_element_opacity(const robo_css_computed_style_t *style)
{
    const robo_css_computed_value_t *value;

    if (!style) {
        return robo_paint_opacity_opaque();
    }
    value = robo_css_computed_style_computed_by_name(style, robo_css_slice_from_text("opacity"));
    if (!value || value->kind != ROBO_CSS_COMPUTED_VALUE_OPACITY) {
        return robo_paint_opacity_opaque();
    }
    return robo_paint_opacity_from_value(robo_css_computed_value_opacity(value));
}

/**
 * @p color with `currentColor` resolved against @p current, and whether it draws.
 *
 * The resolution rule is `robo_css_color_resolve_current`'s and this wrapper exists for
 * the two decisions beside it:
 *
 *   a `NONE` colour asked to draw becomes `currentColor` **only** when @p default_for_none
 *   is set. That is the difference between the background (`background-color`'s initial
 *   value is `transparent`, so an undeclared background draws nothing) and a border
 *   (`border-*-color`'s initial value is `currentColor`, so an undeclared colour draws in
 *   the element's text colour). Encoding it as a flag keeps one code path for the
 *   resolution and states the two initial values where they differ;
 *
 *   a colour that could not be resolved is **still a colour** the operation may carry
 *   (`currentColor` is legal in a record), so the caller learns "contextual" and draws
 *   anyway. That is what `stats.contextual_colors` counts.
 *
 * @param out_color    receives the colour the operation will carry.
 * @param out_draws    receives 1 when the colour puts a pixel down.
 * @param out_contextual receives 1 when the colour stayed `currentColor`.
 */
static void paint_resolve_color(robo_css_color_t color,
                                robo_css_color_t current,
                                int default_for_none,
                                robo_css_color_t *out_color,
                                int *out_draws,
                                int *out_contextual)
{
    robo_css_color_t resolved;

    if (out_color) {
        *out_color = robo_css_color_none();
    }
    if (out_draws) {
        *out_draws = 0;
    }
    if (out_contextual) {
        *out_contextual = 0;
    }

    if (color.kind == ROBO_CSS_COLOR_NONE) {
        if (!default_for_none) {
            /* An undeclared background: nothing to draw, and nothing contextual either --
             * `transparent` is a colour the author effectively asked for. */
            return;
        }
        /* An undeclared border colour is `currentColor` by CSS's initial value. */
        color = robo_css_color_current();
    }
    if (color.kind == ROBO_CSS_COLOR_INVALID) {
        return;
    }

    if (robo_css_color_resolve_current(color, current, &resolved)) {
        if (out_color) {
            *out_color = resolved;
        }
        if (out_draws) {
            /* A fully transparent colour occupies no pixel, which is a statement about
             * the document rather than an omission -- so it is counted as "does not
             * draw" and no operation is emitted for it. */
            *out_draws = resolved.alpha != 0u;
        }
        return;
    }

    /*
     * It stayed contextual: there was no `color` to substitute. The operation still
     * carries it -- a `currentColor` record is valid (RoboPaintOperation.h) -- and the
     * stat says how many did, because a document where *every* colour is contextual is a
     * document whose styles were not computed.
     */
    if (out_color) {
        *out_color = color;
    }
    if (out_draws) {
        *out_draws = 1;
    }
    if (out_contextual) {
        *out_contextual = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Building one operation                                              */
/* ------------------------------------------------------------------ */

/** The four border kinds, indexed by `robo_css_box_side_t`. */
static const robo_paint_op_kind_t paint_border_kinds[ROBO_CSS_BOX_SIDE_COUNT] = {
    ROBO_PAINT_OP_BORDER_TOP,
    ROBO_PAINT_OP_BORDER_RIGHT,
    ROBO_PAINT_OP_BORDER_BOTTOM,
    ROBO_PAINT_OP_BORDER_LEFT
};

/*
 * Emission is one helper per *payload shape* rather than one with a kind switch, because
 * the kinds of record carry different fields and filling the wrong ones is the mistake
 * worth making impossible: a structural record fills a colour that says "nothing", a
 * colour drawing fills a colour a rasteriser can use, a run fills a **span** into the
 * pool it belongs to (11.6) and an image fills a **retained resource** (11.7) -- and
 * none of the four can be spelled by accident from another's helper.
 */

/**
 * Append a **structural** record: a `BEGIN_BOX`/`CONTENT`/`END_BOX`, or one of the two
 * **scope** kinds the same shape carries (`PUSH_CLIP`/`POP_CLIP` from 11.8, and
 * `PUSH_OPACITY`/`POP_OPACITY` from 11.9).
 *
 * The **opacity in force** is a parameter beside the clip rather than a field filled later,
 * and the rule it is filled by is the one rule that makes every record in the list read the
 * same way: a record carries the clip **and** the opacity in force at its own position in the
 * sequence. For a scope pair that means both halves carry the value the pair establishes --
 * which is what makes a pair self-identifying, and what a builder checks the pop against.
 */
static robo_result_t paint_emit_structural(robo_paint_list_t *list,
                                      robo_paint_op_kind_t kind,
                                      robo_layout_node_id_t node,
                                      size_t depth,
                                      const robo_paint_boxes_t *boxes,
                                      robo_layout_point_t origin,
                                      const robo_paint_clip_t *clip,
                                      const robo_paint_opacity_t *opacity,
                                      robo_paint_diag_t *diag)
{
    robo_paint_operation_t operation;

    robo_paint_operation_init(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = depth;
    operation.boxes = *boxes;
    operation.origin = origin;
    operation.clip = *clip;
    operation.opacity = *opacity;
    operation.color = robo_css_color_none();
    return robo_paint_list_append(list, &operation, diag);
}

/**
 * Append a drawing operation: a background, or one border side.
 *
 * It takes **both** of the fields whose meaning varies by kind, and takes them as
 * parameters rather than reading them: a background passes the colour the box declared
 * and `NONE` for the style, a border passes both of its own. That is the shape the
 * record's validity rule enforces from the other side, so a caller cannot half-fill a
 * drawing record by accident -- the two facts a border has and a background does not
 * arrive together.
 */
static robo_result_t paint_emit_drawing(robo_paint_list_t *list,
                                       robo_paint_op_kind_t kind,
                                       robo_layout_node_id_t node,
                                       size_t depth,
                                       const robo_paint_boxes_t *boxes,
                                       robo_layout_point_t origin,
                                       const robo_paint_clip_t *clip,
                                       const robo_paint_opacity_t *opacity,
                                       robo_css_color_t color,
                                       robo_css_border_style_t style,
                                       robo_paint_diag_t *diag)
{
    robo_paint_operation_t operation;

    robo_paint_operation_init(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = depth;
    operation.boxes = *boxes;
    operation.origin = origin;
    operation.clip = *clip;
    operation.opacity = *opacity;
    operation.color = color;
    operation.style = style;
    return robo_paint_list_append(list, &operation, diag);
}

/* ------------------------------------------------------------------ */
/* The decorations of one placed box                                   */
/* ------------------------------------------------------------------ */

/**
 * Append the background and border operations a placed box's own declarations ask for.
 *
 * Four rules, and each one is "emit only when it draws" applied to a different property:
 *
 *   a background is emitted when `background-color` resolved to a colour that puts a
 *   pixel down. An undeclared background is `transparent` and emits nothing -- which is
 *   why a plain `<div>` has no background operation rather than an invisible one;
 *
 *   a border side is emitted when its **used** width is above zero *and* its colour
 *   draws. The width comes from layout's own used record, so a `border: 5px none` side
 *   -- a declared five that costs no room -- is already a zero here and needs no second
 *   decision;
 *
 *   an undeclared border colour is `currentColor`, because that is CSS's initial value
 *   for the longhand and the reason `RoboLayoutBorder.h` reports `NONE` rather than
 *   inventing `transparent`;
 *
 *   `currentColor` that nothing could resolve is still emitted, and counted.
 *
 * The border record is asked for once and its four used sides read through the layer's
 * own width door, so "which field of the used record is the width" is asked where
 * RoboLayout answers it rather than three times here.
 */
static robo_result_t paint_box_decorations(robo_paint_context_t *context,
                                          const robo_layout_tree_t *tree,
                                          robo_layout_node_id_t id,
                                          size_t depth,
                                          const robo_paint_boxes_t *boxes,
                                          robo_layout_point_t origin,
                                          const robo_paint_clip_t *clip,
                                          const robo_paint_opacity_t *opacity,
                                          robo_paint_list_t *list,
                                          robo_paint_diag_t *diag)
{
    const robo_css_computed_style_t *computed = robo_layout_object_style(tree, id);
    robo_css_color_t element_color;
    robo_css_color_t color;
    robo_css_border_style_t style;
    int draws;
    int contextual;
    robo_layout_border_edges_t borders;
    size_t side;

    if (!computed) {
        /* No declaration could be read from this box at all. A fact about the build --
         * the element had no computed style -- and not a failure, so the box contributes
         * a marker pair and no decoration. */
        context->stats.without_style++;
        return ROBO_OK;
    }
    element_color = robo_paint_element_color(computed);

    /* The background: `background-color`, which is `transparent` until an author says
     * otherwise. Painted over the **border box**, CSS's initial `background-clip`. */
    color = robo_paint_property_color(computed, "background-color");
    paint_resolve_color(color, element_color, 0, &color, &draws, &contextual);
    if (contextual) {
        context->stats.contextual_colors++;
    }
    if (draws) {
        robo_result_t result = paint_emit_drawing(list, ROBO_PAINT_OP_BACKGROUND, id,
                                                  depth, boxes, origin, clip, opacity, color,
                                                  ROBO_CSS_BORDER_STYLE_NONE, diag);

        if (result != ROBO_OK) {
            return result;
        }
        context->stats.backgrounds++;
    }

    if (robo_layout_object_borders(tree, id, &borders, NULL) != ROBO_OK) {
        /* Declined (a text run or a transparent inline box never reaches here -- both are
         * refused by `robo_paint_boxes_of` first) or absent. Either way there is no used
         * border to draw, which is an answer and not a failure. */
        return ROBO_OK;
    }

    for (side = 0u; side < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side++) {
        robo_gfx_pixel_x_t width = 0;

        if (robo_layout_border_edges_width(&borders, (robo_css_box_side_t)side, &width) !=
            ROBO_OK) {
            continue;
        }
        if (width <= 0) {
            /* The used width is the whole of "does this side draw": a zero-width border,
             * a `none` style and an undeclared side are one number here, which is the
             * conclusion RoboLayout already reached. */
            continue;
        }

        /*
         * The colour **and** the style come off the same used side and in one switch, so
         * they cannot be read from different sides -- the mistake a second switch would
         * make possible. 10.11 put both on the used record for exactly this moment: the
         * style is not a property this layer reads from a computed style but a fact the
         * layer below already resolved, and a paint pass that read only the colour would
         * be dropping it silently.
         */
        switch (side) {
        case ROBO_CSS_BOX_SIDE_TOP:
            color = borders.top.color;
            style = borders.top.style;
            break;
        case ROBO_CSS_BOX_SIDE_RIGHT:
            color = borders.right.color;
            style = borders.right.style;
            break;
        case ROBO_CSS_BOX_SIDE_BOTTOM:
            color = borders.bottom.color;
            style = borders.bottom.style;
            break;
        default:
            color = borders.left.color;
            style = borders.left.style;
            break;
        }

        paint_resolve_color(color, element_color, 1, &color, &draws, &contextual);
        if (contextual) {
            context->stats.contextual_colors++;
        }
        if (!draws) {
            continue;
        }
        {
            robo_result_t result = paint_emit_drawing(list, paint_border_kinds[side], id,
                                                      depth, boxes, origin, clip, opacity, color,
                                                      style, diag);

            if (result != ROBO_OK) {
                return result;
            }
        }
        context->stats.border_sides++;
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* One text run (11.6)                                                 */
/* ------------------------------------------------------------------ */

/*
 * Three facts this module had none of before this phase, and each of them is a *read*:
 *
 *   what characters the run holds -- the DOM's bytes, decoded through RoboUnicode into
 *   the code points a range counts in;
 *   how wide the placement is -- layout's own result for the object, which for a text
 *   object is the line fragment;
 *   where the baseline sits -- layout's own baseline record, read as an offset from the
 *   top of that placement.
 *
 * Nothing here measures anything. A run's width came from the font through 10.5's seam and
 * was consumed by the line; the painter that computed a width would be a second measurer
 * with a different answer, and the painted text would not line up with the box that was
 * sized for it.
 */

/** The code points of @p handle's @p range, appended to @p list's pool.
 *
 * @return ROBO_OK and the span; ROBO_NOT_FOUND when the node does not resolve or its bytes
 *         do not decode; ROBO_INVALID_ARGUMENT for a range outside the text;
 *         ROBO_OUT_OF_MEMORY.
 */
static robo_result_t paint_read_text(robo_paint_list_t *list,
                                    const robo_html_node_handle_t *handle,
                                    robo_layout_text_range_t range,
                                    robo_paint_text_span_t *out_span,
                                    robo_paint_diag_t *diag)
{
    robo_html_node_view_t view;
    uint32_t *code_points;
    size_t start = 0u;
    size_t length = 0u;
    size_t offset;
    size_t count = 0u;
    robo_result_t result;

    *out_span = robo_paint_text_span_at(0u);
    if (robo_html_node_get_view(handle, &view, NULL) != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_TRAVERSAL, ROBO_NOT_FOUND,
                               "a text run's node did not resolve");
    }
    if (view.kind != ROBO_HTML_NODE_KIND_TEXT) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_TRAVERSAL, ROBO_NOT_FOUND,
                               "a text object names a node that holds no text");
    }
    /*
     * The range's byte window, through the bridge that owns the range type -- so "both
     * endpoints land on code-point boundaries and inside the text" is established once, by
     * the code that knows the convention, rather than by a second copy of the arithmetic
     * here. The **window** is what this uses; the range's code-point endpoints were used by
     * the measurer and are not needed to walk forward from a known boundary.
     */
    if (robo_layout_text_range_bytes(handle, range, &start, &length, NULL) != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_TRAVERSAL, ROBO_INVALID_ARGUMENT,
                               "a text run's range is not inside its node");
    }
    if (length == 0u) {
        /* An empty run is a legal text node and draws nothing; the caller counts it as a
         * run that produced no command rather than as a failure. */
        return ROBO_OK;
    }
    /*
     * One scratch buffer sized by the window's **byte** length, which is an upper bound on
     * its code points -- UTF-8 is never shorter than one byte per code point, so a window of
     * `n` bytes holds at most `n` characters and the decode cannot overflow. The alternative
     * -- decoding into the pool one chunk at a time -- would give a run several spans and
     * break the one thing a span promises (a run is one contiguous `[first, first + count)`).
     */
    code_points = (uint32_t *)robo_calloc(length, sizeof(*code_points));
    if (!code_points) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_TRAVERSAL, ROBO_OUT_OF_MEMORY,
                               "could not hold a text run's characters");
    }
    offset = start;
    while (offset < start + length && count < length) {
        robo_uchar32_t code_point = 0;
        size_t next = offset;

        if (robo_unicode_utf8_decode(view.text, view.text_length, offset,
                                    ROBO_UNICODE_REPLACE, &code_point, &next) !=
                ROBO_UNICODE_OK ||
            next <= offset) {
            robo_free(code_points);
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_TRAVERSAL, ROBO_NOT_FOUND,
                                   "a text run's bytes could not be decoded");
        }
        code_points[count] = (uint32_t)code_point;
        count++;
        offset = next;
    }
    result = robo_paint_list_add_text(list, code_points, count, out_span, diag);
    robo_free(code_points);
    if (result != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_TRAVERSAL, result,
                               "a text run's characters could not be stored");
    }
    return ROBO_OK;
}

/**
 * Emit the `TEXT` operation for text object @p id, or report that there is none to emit.
 *
 * @param out_emitted non-NULL; set to 1 when an operation was appended, 0 when the run
 *                    draws nothing -- which is a **statement about the document** rather
 *                    than a failure, so the caller counts it and the pass continues. The
 *                    reasons are the drawing rule's (`RoboPaintTypes.h`): no colour to
 *                    draw in, no characters, nothing placed it, or a range that could not
 *                    be read.
 *
 * @return ROBO_OK; a failure only when the list could not grow.
 */
static robo_result_t paint_emit_text(robo_paint_context_t *context,
                                    const robo_layout_tree_t *tree,
                                    robo_layout_node_id_t id,
                                    size_t depth,
                                    robo_layout_point_t origin,
                                    const robo_paint_clip_t *clip,
                                    const robo_paint_opacity_t *opacity,
                                    robo_paint_list_t *list,
                                    int *out_emitted,
                                    robo_paint_diag_t *diag)
{
    const robo_css_computed_style_t *computed;
    robo_layout_text_run_t run;
    robo_layout_baseline_metrics_t baseline;
    robo_paint_boxes_t local;
    robo_paint_boxes_t boxes;
    robo_paint_operation_t operation;
    robo_paint_text_run_t text;
    robo_html_node_handle_t handle;
    robo_css_color_t color;
    robo_css_color_t element_color;
    robo_layout_text_style_t font;
    int draws = 0;
    int contextual = 0;
    robo_result_t result;

    *out_emitted = 0;

    /* The placement, through the **same door a box's three rectangles come from**: it
     * declines a text run's inset records and says so with `insets_measured`, so a run's
     * border, padding and content box are one rectangle and no second derivation of "a run
     * has no box layers" exists in this module. */
    if (robo_paint_boxes_of(tree, id, &local, NULL) != ROBO_OK) {
        return ROBO_OK;
    }
    if (robo_layout_object_text_run(tree, id, &run, NULL) != ROBO_OK) {
        return ROBO_OK;
    }
    if (robo_layout_object_dom_node(tree, id, &handle) != ROBO_OK) {
        return ROBO_OK;
    }

    /*
     * The colour, read exactly where a box's is read: the run's style **is its parent
     * element's**, borrowed, because every property a run needs is inherited and RoboCSS
     * already distributed them (`RoboLayoutText.h` argues it). `currentColor` resolves
     * against the same style, which is the element's own `color` -- and an element with no
     * usable `color` draws no text, counted rather than guessed at.
     */
    computed = robo_layout_object_style(tree, id);
    element_color = robo_paint_element_color(computed);
    color = robo_paint_property_color(computed, "color");
    /* `color`'s initial value is not "nothing": an undeclared text colour is the
     * element's resolved colour, so the `default_for_none` flag is set here exactly as it
     * is for a border, and for the same reason. */
    paint_resolve_color(color, element_color, 1, &color, &draws, &contextual);
    if (contextual) {
        context->stats.contextual_colors++;
    }
    if (!draws) {
        return ROBO_OK;
    }

    /*
     * The font inputs, **through the layer that resolved them for measurement** rather than
     * re-read from the computed style here: `robo_layout_text_style_resolve` is the one
     * place a computed `font-size` becomes a pixel size with CSS's initial values applied,
     * and a second reader would be a second answer to "what size is this text". The record
     * then travels with the run, so the size it was measured at is the size it is drawn at
     * by construction.
     */
    if (robo_layout_text_style_resolve(computed, &font) != ROBO_OK) {
        return ROBO_OK;
    }

    if (robo_layout_object_baseline(tree, id, &baseline, NULL) != ROBO_OK) {
        return ROBO_OK;
    }

    robo_paint_text_run_init(&text);
    if (paint_read_text(list, &handle, run.range, &text.span, diag) != ROBO_OK ||
        robo_paint_text_span_is_empty(text.span)) {
        /* A run whose characters could not be read draws nothing. The failure is *this
         * run's*, not the document's: the rest of it is painted, and `text_runs` against
         * `texts` says one run produced no command. */
        return ROBO_OK;
    }
    /*
     * The baseline, through the offset door rather than off `ascent`: the two are equal for
     * every value this build produces, and the door is the one that **says so** -- a painter
     * that read `ascent` would be relying on a convention `RoboLayoutBaseline.h` states as a
     * convention, in a module that is not allowed to know it.
     */
    {
        int32_t offset = 0;

        if (robo_layout_baseline_offset(&baseline, &offset) != ROBO_OK) {
            return ROBO_OK;
        }
        text.baseline_offset = offset;
    }
    text.font = font;

    (void)robo_paint_boxes_translate(&local, origin.x, origin.y, &boxes, NULL);

    robo_paint_operation_init(&operation);
    operation.kind = ROBO_PAINT_OP_TEXT;
    operation.node = id;
    operation.depth = depth;
    operation.boxes = boxes;
    operation.origin = origin;
    operation.clip = *clip;
    /*
     * The opacity **in force**, passed in rather than read from `computed` -- and this is the
     * one place in this file where a style is deliberately *not* consulted. A run borrows the
     * style of the element that owns the text node, and that element opened its own opacity
     * scope around this run before the run was reached. Reading the declaration again here
     * would apply it twice, so a `0.5` element's text would draw at `0.25` while its
     * background drew at `0.5`. The value the traversal is carrying is the answer; the style
     * is the question that was already asked one level up.
     */
    operation.opacity = *opacity;
    operation.color = color;
    operation.text = text;
    result = robo_paint_list_append(list, &operation, diag);
    if (result != ROBO_OK) {
        return result;
    }
    *out_emitted = 1;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* One image (11.7)                                                    */
/* ------------------------------------------------------------------ */

/*
 * An image is the first drawing kind whose appearance this module does **not** resolve, and
 * that is the whole of why it needs its own site rather than one more branch in the decoration
 * pass: a background's colour comes from the box's computed style and a border's from its used
 * record, where an image's pixels come from a **resource** the document attached to the element.
 * So the reads are different, and the two reads this site makes are both RoboHTML's own doors --
 * `robo_layout_object_dom_node` for the element a layout object was built for (the same door
 * 11.6's text branch uses for its characters) and `robo_html_element_image_get` for the resource
 * attached to it.
 *
 * Nothing here fetches, decodes or sizes anything. A `src` is a fact about the document and was
 * resolved before the paint pass ran; the destination rectangle is the box's **content box**,
 * which is layout's answer to "where does this element's content go" and the same answer for a
 * replaced element as for any other. Intrinsic sizing -- an `<img>` that is as large as its
 * resource without a CSS declaration -- is RoboLayout's work and is not implemented for replaced
 * elements in this build, which is why a document must size its images (`paint/docs/Images.md`
 * states the limitation in full).
 */

/**
 * Emit the `IMAGE` operation for the box of image element @p id, if that is what the box is.
 *
 * @param out_is_image  non-NULL; set to 1 when the box's element **is** an `<img>`, which is the
 *                      fact the caller counts. A box that is not an image element is not a
 *                      skipped image -- it is simply not this site's business -- and the two are
 *                      kept apart here rather than inferred from a count afterwards.
 * @param out_emitted   non-NULL; set to 1 when an operation was appended, 0 when there is no
 *                      image to draw -- a **statement about the document** rather than a failure,
 *                      so the caller counts it and the pass continues. The reasons are the
 *                      missing-image policy's: no `src`, nothing resolved, a decode that failed
 *                      or was refused, a resource without a pixel surface, or dimensions this
 *                      build will not carry into a record (`RoboPaintImage.h`).
 *
 * @return ROBO_OK; a failure only when the list could not grow.
 */
static robo_result_t paint_emit_image(const robo_layout_tree_t *tree,
                                     robo_layout_node_id_t id,
                                     size_t depth,
                                     const robo_paint_boxes_t *boxes,
                                     robo_layout_point_t origin,
                                     const robo_paint_clip_t *clip,
                                     const robo_paint_opacity_t *opacity,
                                     robo_paint_list_t *list,
                                     int *out_is_image,
                                     int *out_emitted,
                                     robo_paint_diag_t *diag)
{
    robo_html_node_handle_t handle;
    robo_html_image_t *resource = NULL;
    robo_gfx_image_t *pixels;
    robo_paint_operation_t operation;
    robo_paint_image_t payload;
    robo_result_t result;

    *out_is_image = 0;
    *out_emitted = 0;

    if (robo_layout_object_dom_node(tree, id, &handle) != ROBO_OK) {
        /* A box with no element behind it -- a generated one -- is not an image element, and
         * that is an answer rather than a failure. */
        return ROBO_OK;
    }
    /*
     * The element-level question first, and through the element model rather than through a
     * string comparison: `robo_html_element_is_tag` is the door the demo's own adapter uses to
     * ask the same thing, and it is what makes "this box is an `<img>`" one predicate instead of
     * a tag lookup repeated wherever a layer cares about images.
     */
    if (!robo_html_element_is_tag(&handle, ROBO_HTML_TAG_IMG)) {
        return ROBO_OK;
    }
    *out_is_image = 1;

    /*
     * The resource, borrowed. A getter that answers anything but ROBO_OK, and a NULL resource,
     * are one answer here: there is nothing attached to this element to draw. A `src` that names
     * nothing, a `src` no resolver has run for, a fetch that failed and bytes that would not
     * decode all arrive as "no resource" or as a resource whose state is not DECODED, and all of
     * them are counted rather than drawn -- the missing-image policy the phase states.
     */
    if (robo_html_element_image_get(&handle, &resource, NULL) != ROBO_OK || !resource) {
        return ROBO_OK;
    }
    if (robo_html_image_state(resource) != ROBO_HTML_IMAGE_STATE_DECODED) {
        return ROBO_OK;
    }
    pixels = robo_html_image_graphics(resource);
    /*
     * The payload, and it is the *record* layer's constructor that decides whether these pixels
     * are drawable at all: a resource with no surface, or with a zero or absurd dimension, is
     * refused there rather than here, so "which combinations are a drawing" has one home
     * (`robo_paint_image_from_resource`). The diagnostic is deliberately not taken -- the refusal
     * is a counted skip, and a `diag` filled for a legitimate outcome would make a caller's
     * error path fire on an ordinary document.
     */
    if (robo_paint_image_from_resource(pixels, &payload, NULL) != ROBO_OK) {
        return ROBO_OK;
    }

    robo_paint_operation_init(&operation);
    operation.kind = ROBO_PAINT_OP_IMAGE;
    operation.node = id;
    operation.depth = depth;
    /*
     * The three rectangles, in page space, and the **content box** among them is the image's
     * destination rectangle -- resolved one stage along, when the display builder turns this
     * record into a `draw-image` command. It is carried rather than stored a second time because
     * the record's own shape already says where content goes, and a fourth rectangle would be a
     * second answer to a question layout has already answered.
     */
    operation.boxes = *boxes;
    operation.origin = origin;
    operation.clip = *clip;
    /* The box's own opacity, passed in rather than read from its style -- the arrangement the
     * text site states in full, and for the same reason: the box opened its own scope, and
     * reading the declaration again here would apply it twice. */
    operation.opacity = *opacity;
    /* No colour and no style: an image record's appearance is its resource's pixels, so the
     * colour field stays `none`, which is what `robo_paint_operation_is_valid` requires of it. */
    operation.image = payload;
    result = robo_paint_list_append(list, &operation, diag);
    if (result != ROBO_OK) {
        return result;
    }
    *out_emitted = 1;
    /*
     * The pass's own counters are deliberately **not** touched here: they belong to the caller,
     * beside the one place that knows whether the box it walked was an image element at all. A
     * site that counted its own trades one reader's mistake for a wrong total.
     */
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The walk                                                            */
/* ------------------------------------------------------------------ */

/** The boundary @p box's overflow puts in force for its contents, or @p clip unchanged.
 *
 * The clip edge is the **padding box**: CSS clips a box's overflowing contents at its
 * padding edge, and `robo_layout_overflow_edges_clip_rect` reads the same box one layer
 * down. Nothing here decides whether to clip -- the box's own used modes say that, and a
 * boundary that hides neither axis leaves the clip in force untouched, which is the case
 * every box in an ordinary document produces.
 */
static robo_result_t paint_child_clip(robo_paint_context_t *context,
                                     const robo_layout_tree_t *tree,
                                     robo_layout_node_id_t id,
                                     const robo_paint_boxes_t *boxes,
                                     const robo_paint_clip_t *clip,
                                     robo_paint_clip_t *out,
                                     robo_paint_diag_t *diag)
{
    robo_layout_overflow_edges_t overflow;

    *out = *clip;
    if (robo_layout_object_overflow(tree, id, &overflow, NULL) != ROBO_OK) {
        return ROBO_OK;
    }
    if (!robo_layout_overflow_edges_clips_any(&overflow)) {
        return ROBO_OK;
    }    context->stats.clipped++;
    return robo_paint_clip_intersect(clip, &boxes->padding, overflow.horizontal,
                                    overflow.vertical, out, diag);
}

/**
 * The opacity @p box's own declaration puts in force for **itself and everything under it**.
 *
 * The product of the strength already in force and this box's own, which is the whole of the
 * arithmetic: opacity composes along a path, so a `0.5` box inside a `0.5` box is a `0.25`
 * subtree. Nothing here decides *whether* to open a scope -- the caller compares the result
 * against what was in force, exactly as the clip's caller does, so both state kinds open a
 * scope under one rule: **only when the value in force actually changes**.
 *
 * `opacity_declared` is counted here, beside the read that finds it, and it counts a fact
 * about the *document* -- boxes whose declaration is not fully opaque -- in exactly the way
 * `clipped` counts boxes whose used modes hide an axis. Both are counted only for a **placed**
 * box, because both are reached on the path this pass paints; an unplaced box is counted in
 * `unplaced` and nothing below it is asked about.
 *
 * Two cases that are not failures and are worth naming, since this is where a reader will look
 * for them: a box with **no computed style** declares nothing and is opaque (the same answer
 * `without_style` gives one layer down), and a declaration that arrived **out of range** was
 * already clamped where it was computed (`RoboCSSOpacity.h`), so what reaches the product is a
 * record this build can draw with.
 *
 * The one deviation from CSS this inherits is `RoboPaintOpacity.h`'s and not this call's: the
 * value is applied per drawing rather than to a flattened subtree, because the group form
 * needs an offscreen layer this build has no compositor for.
 */
static robo_paint_opacity_t paint_child_opacity(robo_paint_context_t *context,
                                               const robo_layout_tree_t *tree,
                                               robo_layout_node_id_t id,
                                               const robo_paint_opacity_t *opacity)
{
    robo_paint_opacity_t declared = robo_paint_element_opacity(robo_layout_object_style(tree, id));

    if (!robo_paint_opacity_is_opaque(&declared)) {
        context->stats.opacity_declared++;
    }
    return robo_paint_opacity_multiply(opacity, &declared);
}

/**
 * Paint @p id and, under it, everything it contains.
 *
 * @param placed  non-zero when @p origin is a real containing-block content origin --
 *                that is, when every ancestor was a placement. A box reached under a box
 *                nothing placed has no origin to be painted at, so it is counted and its
 *                subtree is walked for the counts and emitted nowhere.
 */
static robo_result_t paint_walk(robo_paint_context_t *context,
                                const robo_layout_tree_t *tree,
                                robo_layout_node_id_t id,
                                size_t depth,
                                int placed_ancestor,
                                robo_layout_point_t origin,
                                const robo_paint_clip_t *clip,
                                const robo_paint_opacity_t *opacity,
                                robo_paint_list_t *list,
                                robo_paint_diag_t *diag)
{
    robo_layout_object_kind_t kind = robo_layout_object_kind(tree, id);
    robo_layout_node_id_t child;
    int placed = 0;
    robo_paint_boxes_t local;
    robo_paint_boxes_t boxes;
    robo_layout_point_t child_origin = origin;
    robo_paint_clip_t child_clip = *clip;
    robo_paint_opacity_t child_opacity = *opacity;
    int clip_scope = 0;
    int opacity_scope = 0;

    if (!robo_layout_object_kind_is_known(kind) || kind == ROBO_LAYOUT_OBJECT_NONE) {
        /* A child link that names nothing. Reported rather than skipped: a pass that
         * painted the rest of the document and said nothing would hand back a list
         * nobody asked for (RoboPaintContext.h). */
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_TRAVERSAL, ROBO_ERROR,
                               "a child link names no object in this tree");
    }

    context->stats.visited++;
    if (depth > context->stats.depth_max) {
        context->stats.depth_max = (uint32_t)depth;
    }

    if (kind == ROBO_LAYOUT_OBJECT_TEXT) {
        /*
         * A text run (11.6): a leaf, so there is nothing below it to walk, and one
         * **drawing** operation rather than a structural triple -- because a run is not a
         * box and the `BEGIN_BOX`/`CONTENT`/`END_BOX` skeleton is a statement about boxes.
         *
         * `placed_ancestor` is honoured here exactly as it is for a box, and for the same
         * reason: a run's paint coordinates are its parent's content origin plus what
         * layout wrote, so a run inside a box nothing placed has no coordinates to be
         * emitted at. The two counters split the runs the traversal reached into the ones
         * that produced a command and the ones that draw nothing -- an identity by
         * construction (`RoboPaintContext.h`), so "this document has text and none of it
         * was painted" is a number a reader sees rather than a silence.
         */
        int emitted = 0;

        context->stats.text_runs++;
        if (placed_ancestor &&
            paint_emit_text(context, tree, id, depth, origin, clip, opacity, list, &emitted,
                            diag) != ROBO_OK) {
            return diag ? diag->code : ROBO_ERROR;
        }
        if (emitted) {
            context->stats.texts++;
        } else {
            context->stats.text_skipped++;
        }
        return ROBO_OK;
    }

    /* The one number a later paint-invalidation pass owes, counted where it can be:
     * a box whose only staleness is paint-visible. Read through layout's own door, so
     * "does this box need layout?" is answered by the model that maintains the bits. */
    {
        uint32_t state = robo_layout_tree_invalidation_state(tree, id);

        if ((state & ROBO_LAYOUT_INVALIDATION_STYLE) != 0u &&
            (state & ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS) == 0u) {
            context->stats.style_only++;
        }
    }

    if (placed_ancestor &&
        robo_paint_boxes_of(tree, id, &local, NULL) == ROBO_OK) {
        placed = 1;
    }
    if (!placed) {
        /* Nothing placed this box, so there is no rectangle and no content origin for
         * anything inside it. Counted, walked for the counts, and emitted nowhere. */
        context->stats.unplaced++;
        child = ROBO_LAYOUT_NODE_ID_NONE;
        if (robo_layout_object_first_child(tree, id, &child) != ROBO_OK) {
            return ROBO_OK;
        }
        while (child != ROBO_LAYOUT_NODE_ID_NONE) {
            robo_result_t result = paint_walk(context, tree, child, depth + 1u, 0, origin,
                                              clip, opacity, list, diag);

            if (result != ROBO_OK) {
                return result;
            }
            if (robo_layout_object_next_sibling(tree, child, &child) != ROBO_OK) {
                break;
            }
        }
        return ROBO_OK;
    }

    /* Into one page space: the accumulated origin added to what layout wrote. */
    (void)robo_paint_boxes_translate(&local, origin.x, origin.y, &boxes, NULL);

    /*
     * The box's **own** opacacity, and the whole of what this site decides: the strength in
     * force for the box and everything under it is the outer strength times the box's
     * declaration, and the scope below opens only when that product differs from what was
     * already in force.
     */
    child_opacity = paint_child_opacity(context, tree, id, opacity);
    opacity_scope = !robo_paint_opacity_equal(&child_opacity, opacity);

    {
        robo_result_t result = paint_emit_structural(list, ROBO_PAINT_OP_BEGIN_BOX, id,
                                                     depth, &boxes, origin, clip, opacity,
                                                     diag);

        if (result != ROBO_OK) {
            return result;
        }
    }
    context->stats.boxes++;

    /*
     * The **opacity scope** (11.9), opened immediately after the box's own marker and before
     * anything the box draws.
     *
     * Its placement is the one structural departure from the clip scope, and it is not a style
     * choice: **a clip belongs to a box's contents and an opacity belongs to the box itself.**
     * CSS paints an element's own `background-color` and borders at the element's opacity --
     * a `0.5` red box is a half-transparent red box -- so the scope must already be open when
     * the first decoration is emitted. The clip cannot be, because the same CSS paints that
     * box's own background under its *parent's* boundary; that is why the one pair opens at the
     * box's content boundary and the other at its marker.
     *
     * The two pairs therefore nest differently around the same box, and the rule a reader can
     * use on both is a single rule: **every record carries the state in force at its own
     * position in the sequence.** `BEGIN_BOX` stands before the push, so it carries the
     * enclosing strength; the decorations, the content boundary, the subtree and the pop all
     * stand inside it and carry the box's; and `END_BOX` stands after the pop, so it carries
     * the enclosing strength again. That is the same rule that makes `POP_CLIP` and `END_BOX`
     * disagree about the clip, and the same rule that makes a pair self-identifying -- both
     * halves of a scope carry the value the pair establishes.
     */
    if (opacity_scope) {
        robo_result_t result = paint_emit_structural(list, ROBO_PAINT_OP_PUSH_OPACITY, id,
                                                     depth, &boxes, origin, clip,
                                                     &child_opacity, diag);

        if (result != ROBO_OK) {
            return result;
        }
        context->stats.opacity_scopes++;
    }

    /*
     * The decorations carry the box's own depth, so that a decoration and its marker can
     * never disagree about how deep the box is, and they carry the **box's own** opacity
     * rather than the enclosing one -- see the scope above.
     */
    {
        robo_result_t result = paint_box_decorations(context, tree, id, depth, &boxes,
                                                     origin, clip, &child_opacity, list, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }

    /*
     * The box's **content image** (11.7), emitted inside the box's own group -- after its
     * decorations and before its content boundary.
     *
     * The placement is CSS's paint order for a **replaced element**, and it is the one thing
     * that distinguishes this record from a text run: an `<img>` has no object of its own, so
     * what its resource draws *is* the element's content, painted after the element's own
     * background and border and before anything the element contains. A `TEXT` record is emitted
     * a level *below* its box for the opposite reason -- a run **is** an object, and the box it
     * belongs to is its parent -- so the two drawing kinds that carry no colour reach the stream
     * at two different depths, and both because the tree says so.
     *
     * The counters are split here rather than inside the site because "the box's element is an
     * image" and "the image drew" are different facts: `image_elements` counts the first,
     * `images` and `image_skipped` split it. A document with no `<img>` at all therefore leaves
     * all three at zero, which is how a reader tells it apart from one whose every image failed.
     */
    {
        int is_image = 0;
        int emitted = 0;

        if (paint_emit_image(tree, id, depth, &boxes, origin, clip, &child_opacity,
                             list, &is_image, &emitted, diag) != ROBO_OK) {
            return diag ? diag->code : ROBO_ERROR;
        }
        if (is_image) {
            context->stats.image_elements++;
            if (emitted) {
                context->stats.images++;
            } else {
                context->stats.image_skipped++;
            }
        }
    }

    /*
     * The **content boundary**, emitted for every placed box whether or not it declared
     * a background, drew a border or has children -- because what it states is a fact
     * about the sequence. It is the explicit end of this box's own records and the start
     * of its subtree's: a consumer reads a box's own painting as `[BEGIN_BOX .. CONTENT]`
     * and its contents as `(CONTENT .. END_BOX)` without having to infer the boundary
     * from the depth of the next child's marker, and a childless box has a boundary just
     * as a parent does. It is a structural record, so it draws nothing and carries no
     * colour (RoboPaintTypes.h).
     */
    {
        robo_result_t result = paint_emit_structural(list, ROBO_PAINT_OP_CONTENT, id, depth,
                                                     &boxes, origin, clip, &child_opacity,
                                                     diag);

        if (result != ROBO_OK) {
            return result;
        }
    }

    if (paint_child_clip(context, tree, id, &boxes, clip, &child_clip, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_ERROR;
    }
    /*
     * The **clip scope** (11.8), and the one condition that opens one: the boundary in force
     * for this box's contents must actually **differ** from the boundary the box itself was
     * entered under.
     *
     * The two are equal in the overwhelmingly common cases, and both are worth stating because
     * they are different documents: a box whose `overflow` is `visible` does not clip
     * (`paint_child_clip` returns the clip unchanged), and a box whose `overflow` is `hidden`
     * inside an ancestor that already bounded the same region does not narrow anything either.
     * Neither establishes a scope, and the count of the scopes this traversal *declines* to
     * push is exactly `stats.clipped` minus `stats.clip_scopes` (`RoboPaintContext.h`).
     *
     * It is emitted **after the box's content boundary** and left open until after the box's
     * last child, which is the arrangement the record's own comment argues: the boundary clips
     * the contents, and the box's own background and border are painted under the boundary it
     * was entered under rather than its own.
     */
    clip_scope = !robo_paint_clip_equal(&child_clip, clip);
    if (clip_scope) {
        robo_result_t result = paint_emit_structural(list, ROBO_PAINT_OP_PUSH_CLIP, id,
                                                     depth, &boxes, origin, &child_clip,
                                                     &child_opacity, diag);

        if (result != ROBO_OK) {
            return result;
        }
        context->stats.clip_scopes++;
    }
    /* The containing block's content **origin**, in paint coordinates: the child's
     * layout geometry is relative to this point, which is what makes `local = boxes -
     * origin` the space layout actually wrote (RoboPaintOperation.h). */
    child_origin = robo_gfx_point(boxes.content.x, boxes.content.y);

    child = ROBO_LAYOUT_NODE_ID_NONE;
    if (robo_layout_object_first_child(tree, id, &child) == ROBO_OK) {
        while (child != ROBO_LAYOUT_NODE_ID_NONE) {
            robo_result_t result = paint_walk(context, tree, child, depth + 1u, placed,
                                              child_origin, &child_clip, &child_opacity, list,
                                              diag);

            if (result != ROBO_OK) {
                return result;
            }
            if (robo_layout_object_next_sibling(tree, child, &child) != ROBO_OK) {
                break;
            }
        }
    }

    /*
     * The scope closes with the box it belongs to, and the pop carries the **same clip the
     * push carried** -- not the boundary being restored. That is what makes the pair
     * self-identifying: a builder checks the pop against the innermost open scope by comparing
     * the two records, and a consumer replaying the list restores the enclosing boundary by
     * popping its own stack rather than by reading a second value out of the payload.
     *
     * It is emitted before the box's `END_BOX`, so the box marker pair stays contiguous around
     * everything the box owns -- its own records and its subtree, scope included.
     */
    if (clip_scope) {
        robo_result_t result = paint_emit_structural(list, ROBO_PAINT_OP_POP_CLIP, id,
                                                     depth, &boxes, origin, &child_clip,
                                                     &child_opacity, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }

    /*
     * The opacity scope closes with the box it belongs to, immediately before the box's own
     * `END_BOX` -- so the marker pair `[BEGIN_BOX .. END_BOX]` brackets the scope and its
     * contents exactly as it brackets the clip scope inside it.
     *
     * Both halves of the pair carry **the same record the push carried**: the pop's value is
     * what the scope established, not what it restores. That is what makes a pair checkable
     * from a stream -- a builder compares the pop against the innermost open scope and a
     * consumer restores the enclosing strength by popping its own stack -- and it is the rule
     * the clip pair already states one field along. A debug dump resolves that the pop's value
     * matches the push's, both are the scope's strength, and the enclosing strength is
     * whatever the sibling before it used.
     */
    if (opacity_scope) {
        robo_result_t result = paint_emit_structural(list, ROBO_PAINT_OP_POP_OPACITY, id,
                                                     depth, &boxes, origin, clip,
                                                     &child_opacity, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }

    {
        robo_result_t result = paint_emit_structural(list, ROBO_PAINT_OP_END_BOX, id,
                                                     depth, &boxes, origin, clip, opacity,
                                                     diag);

        if (result != ROBO_OK) {
            return result;
        }
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The context                                                         */
/* ------------------------------------------------------------------ */

robo_result_t robo_paint_context_create(robo_paint_context_t **out_context,
                                        robo_paint_diag_t *diag)
{
    robo_paint_context_t *context;

    if (!out_context) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CONTEXT, ROBO_INVALID_ARGUMENT,
                               "out_context is NULL");
    }
    *out_context = NULL;
    context = (robo_paint_context_t *)robo_calloc(1u, sizeof(*context));
    if (!context) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CONTEXT, ROBO_OUT_OF_MEMORY,
                               "could not allocate the paint context");
    }
    /* calloc already gives the empty state -- zeroed counters, outcome NONE, no tree.
     * The explicit assignment is here so that the definition of "empty" lives in one
     * place rather than in the allocator's behaviour. */
    context->outcome = ROBO_PAINT_PASS_NONE;
    context->tree = NULL;
    context->user_data = NULL;
    *out_context = context;
    return ROBO_OK;
}

void robo_paint_context_destroy(robo_paint_context_t *context)
{
    /* The tree, the list and the caller's user data are references, so there is nothing
     * to release but the struct itself -- which is what makes the destroy safe in any
     * order relative to the things it was used with. */
    robo_free(context);
}

void robo_paint_context_reset(robo_paint_context_t *context)
{
    if (!context) {
        return;
    }
    memset(&context->stats, 0, sizeof(context->stats));
    context->outcome = ROBO_PAINT_PASS_NONE;
    context->tree = NULL;
    /* user_data is deliberately left alone: it belongs to the caller, and a reset that
     * dropped it would break a caller that installs it once and resets per pass -- the
     * normal way to use this door, and RoboLayout's context makes the same call. */
}

void robo_paint_context_set_user_data(robo_paint_context_t *context, void *user_data)
{
    if (context) {
        context->user_data = user_data;
    }
}

void *robo_paint_context_user_data(const robo_paint_context_t *context)
{
    return context ? context->user_data : NULL;
}

robo_result_t robo_paint_context_run(robo_paint_context_t *context,
                                    const robo_layout_tree_t *tree,
                                    robo_paint_list_t *list,
                                    robo_paint_diag_t *diag)
{
    size_t completed;
    robo_layout_node_id_t root;
    robo_result_t result;

    if (!context || !tree || !list) {
        if (context) {
            context->outcome = ROBO_PAINT_PASS_FAILED;
        }
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CONTEXT, ROBO_INVALID_ARGUMENT,
                               "context, tree and list are all required");
    }

    /*
     * Per-pass fields are reset; `passes` is cumulative and is not -- the same split
     * RoboLayout's context uses, and for the same reason: a caller reading stats after a
     * second pass wants that pass's numbers, and a caller asking how many passes have run
     * wants the total. Zeroing the record and then incrementing makes `passes`
     * permanently 1, which reads as "this context ran once" for a context that ran five
     * times.
     */
    completed = context->stats.passes;
    memset(&context->stats, 0, sizeof(context->stats));
    context->stats.passes = completed;
    context->stats.objects = robo_layout_tree_object_count(tree);
    context->tree = tree;

    /*
     * The list is cleared first, so a caller may reuse one list across passes and never
     * sees a previous document's operations. Clearing before the staleness check rather
     * than after is deliberate: a refused pass hands back an **empty** list rather than
     * the previous pass's, which is the only answer a caller can act on.
     */
    robo_paint_list_clear(list);

    /*
     * A tree whose geometry is not final is refused rather than painted
     * (RoboPaintContext.h): a list built from half-computed geometry is a wrong list, and
     * a caller that wanted a preview can run a layout pass first.
     */
    if (robo_layout_tree_needs_layout(tree)) {
        /*
         * A **fourth outcome, not a fifth error**: the walk did not run, the list is
         * empty, and `ROBO_OK` is the honest result code because nothing went wrong that
         * a caller cannot fix. A caller acts on the outcome; one that reads only the code
         * sees success with an empty list, which is exactly what a refused pass is.
         */
        context->outcome = ROBO_PAINT_PASS_STALE;
        context->stats.passes++;
        return ROBO_OK;
    }

    root = robo_layout_tree_root(tree);
    if (root == ROBO_LAYOUT_NODE_ID_NONE) {
        /*
         * No root box: a document whose root element generates no box, or one that builds
         * no boxes at all. A legitimate pass that paints nothing -- `objects` is already
         * 0 and every counter is already right -- rather than an error.
         */
        context->outcome = ROBO_PAINT_PASS_PAINTED;
        context->stats.passes++;
        return ROBO_OK;
    }

    /*
     * The root is entered under the two states a document starts in: a boundary that hides
     * neither axis, and full strength. Neither is a *scope* -- both are what an empty stack
     * answers -- so they are stated here as the initial values rather than pushed as commands,
     * which is exactly why a document that declares no opacity anywhere produces a list with
     * no opacity command in it.
     *
     * The strength comes from the opacity module's own door rather than from a written `1.0`,
     * for the reason the clip's modes come from the overflow vocabulary: "fully opaque" is a
     * fact about CSS that the layer owning the property states, and a build that changed the
     * property's initial value would change one number rather than one number and a copy here.
     */
    {
        robo_paint_opacity_t opaque = robo_paint_opacity_opaque();

        result = paint_walk(context, tree, root, 0u, 1, robo_gfx_point(0, 0),
                            &(robo_paint_clip_t){ { ROBO_CSS_OVERFLOW_VISIBLE, 0, 0 },
                                                  { ROBO_CSS_OVERFLOW_VISIBLE, 0, 0 } },
                            &opaque,
                            list, diag);
    }
    if (result != ROBO_OK) {
        context->outcome = ROBO_PAINT_PASS_FAILED;
        return result;
    }

    context->stats.ops = robo_paint_list_count(list);
    context->outcome = ROBO_PAINT_PASS_PAINTED;
    context->stats.passes++;
    return ROBO_OK;
}

void robo_paint_context_stats(const robo_paint_context_t *context, robo_paint_stats_t *out)
{
    if (!out) {
        return;
    }
    if (!context) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = context->stats;
}

robo_paint_pass_outcome_t robo_paint_context_outcome(const robo_paint_context_t *context)
{
    return context ? context->outcome : ROBO_PAINT_PASS_NONE;
}

const robo_layout_tree_t *robo_paint_context_tree(const robo_paint_context_t *context)
{
    return context ? context->tree : NULL;
}
