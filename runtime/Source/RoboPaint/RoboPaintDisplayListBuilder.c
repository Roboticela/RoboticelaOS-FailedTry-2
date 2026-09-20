/*
 * RoboDisplay list builder (Phase 11.3) — see RoboPaintDisplayListBuilder.h.
 *
 * The conversion layer and the structural check, and the two are deliberately one pass
 * rather than two: a record is validated in the same step it is converted, so a record
 * that is out of place is refused before any command derived from it is appended -- and
 * the output list is cleared on refusal, so a caller never holds a partially converted
 * sequence.
 *
 * The structural check is a small state machine over a stack of the currently open boxes.
 * The reason it is a stack rather than a running counter is the *node identity* rule: a
 * box's content boundary and its closing marker must name the box its opening marker
 * named, and "which box is this depth?" is only answerable by remembering the open boxes.
 * The stack is grown on demand, and it is why the builder is a stateful object rather than
 * a function -- the capacity survives a `reset`, exactly as a paint context's reuse does.
 *
 * Nothing here performs layout or touches the tree: the records are read through the paint
 * list's `const` doors and the conversion uses only what a record already carries (plus
 * `robo_paint_operation_side_band`, the one derivation the record leaves to its consumer).
 */

#include "RoboPaint/RoboPaintDisplayListBuilder.h"

#include "RoboPaint/RoboPaintOperation.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/** One open box: its node id and whether its content boundary has passed. */
typedef struct display_open_box {
    robo_layout_node_id_t node;
    unsigned char content_seen;
} display_open_box_t;

struct robo_paint_display_list_builder {
    display_open_box_t *open;
    size_t open_count;
    size_t open_capacity;
};

/* ------------------------------------------------------------------ */
/* The open-box stack                                                  */
/* ------------------------------------------------------------------ */

static robo_result_t display_stack_reserve(robo_paint_display_list_builder_t *builder,
                                           size_t capacity,
                                           robo_paint_diag_t *diag)
{
    display_open_box_t *grown;

    if (capacity <= builder->open_capacity) {
        return ROBO_OK;
    }
    grown = (display_open_box_t *)robo_realloc(builder->open,
                                               capacity * sizeof(*grown));
    if (!grown) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_OUT_OF_MEMORY,
                               "could not grow the builder's open-box stack");
    }
    builder->open = grown;
    builder->open_capacity = capacity;
    return ROBO_OK;
}

static robo_result_t display_stack_push(robo_paint_display_list_builder_t *builder,
                                        robo_layout_node_id_t node,
                                        robo_paint_diag_t *diag)
{
    robo_result_t result;

    if (builder->open_count == builder->open_capacity) {
        size_t wanted = builder->open_capacity < 1u ? 8u : builder->open_capacity * 2u;

        result = display_stack_reserve(builder, wanted, diag);
        if (result != ROBO_OK) {
            return result;
        }
    }
    builder->open[builder->open_count].node = node;
    builder->open[builder->open_count].content_seen = 0u;
    builder->open_count++;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

robo_result_t robo_paint_display_list_builder_create(robo_paint_display_list_builder_t **out_builder,
                                               robo_paint_diag_t *diag)
{
    robo_paint_display_list_builder_t *builder;

    if (!out_builder) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_INVALID_ARGUMENT,
                               "out_builder is NULL");
    }
    *out_builder = NULL;
    builder = (robo_paint_display_list_builder_t *)robo_calloc(1u, sizeof(*builder));
    if (!builder) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_OUT_OF_MEMORY,
                               "could not allocate the display list builder");
    }
    /* No first block: the stack grows on the first box, and an empty build never allocates
     * one -- which is the honest baseline for a builder that is reused across builds. */
    *out_builder = builder;
    return ROBO_OK;
}

void robo_paint_display_list_builder_destroy(robo_paint_display_list_builder_t *builder)
{
    if (!builder) {
        return;
    }
    robo_free(builder->open);
    robo_free(builder);
}

void robo_paint_display_list_builder_reset(robo_paint_display_list_builder_t *builder)
{
    if (!builder) {
        return;
    }
    /* The capacity survives: a builder reused per frame reuses its stack. */
    builder->open_count = 0u;
}

/* ------------------------------------------------------------------ */
/* Conversion                                                          */
/* ------------------------------------------------------------------ */

/** Fill @p out with the display command @p operation resolves to.
 *
 * @param text_target the output list's text pool. A `TEXT` record's characters are copied
 *                    into it here, because a display command names characters in **its
 *                    own** list's pool -- the record list's pool is the other list's, and a
 *                    span that still pointed there would be a command that stopped meaning
 *                    anything the moment that list was cleared. Passing NULL is legal and
 *                    makes a text record resolve to an error, which only a caller that
 *                    built a list with no pool could do.
 *
 * @return ROBO_OK; ROBO_ERROR for an operation this build has no rule for, a border whose
 *         band cannot be derived, or a text run whose characters cannot be copied.
 */
static robo_result_t display_command_for(const robo_paint_operation_t *operation,
                                         const robo_paint_text_pool_t *text_source,
                                         robo_paint_text_pool_t *text_target,
                                         robo_paint_display_command_t *out,
                                         robo_paint_diag_t *diag)
{
    robo_paint_display_command_init(out);
    out->node = operation->node;
    out->depth = operation->depth;
    out->clip = operation->clip;

    switch (operation->kind) {
    case ROBO_PAINT_OP_BEGIN_BOX:
        out->kind = ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE;
        out->rect = operation->boxes.border;
        /* Structural: the colour is `none`, which `init` already left it as. */
        return ROBO_OK;
    case ROBO_PAINT_OP_BACKGROUND:
        out->kind = ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND;
        out->rect = operation->boxes.border;
        out->color = operation->color;
        return ROBO_OK;
    case ROBO_PAINT_OP_BORDER_TOP:
    case ROBO_PAINT_OP_BORDER_RIGHT:
    case ROBO_PAINT_OP_BORDER_BOTTOM:
    case ROBO_PAINT_OP_BORDER_LEFT:
        out->kind = ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER;
        if (robo_paint_operation_side_band(operation, &out->rect, diag) != ROBO_OK) {
            return ROBO_ERROR;
        }
        /* The **side** is consumed into the band rectangle and the four kinds collapse into
         * one -- but the **style** is not consumed by anything: it is the shape of what is
         * inside that rectangle, so it travels with the command (11.5). */
        out->color = operation->color;
        out->style = operation->style;
        return ROBO_OK;
    case ROBO_PAINT_OP_TEXT:
        out->kind = ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT;
        /* A run has no insets, so the one rectangle it acts on is any of the three the
         * operation carries -- they are equal by construction (`robo_paint_boxes_of`
         * declines a text run's inset doors and says so with `insets_measured`). */
        out->rect = operation->boxes.border;
        out->color = operation->color;
        out->text = operation->text;
        /* The characters are renumbered into this list's pool. The run's other two facts
         * -- the baseline offset and the font inputs -- are values and copy straight
         * across, so "the size it was measured at is the size it will be drawn at" is a
         * copy rather than an agreement to maintain. */
        if (robo_paint_text_pool_copy(text_target, text_source, operation->text.span,
                                      &out->text.span, diag) != ROBO_OK) {
            return ROBO_ERROR;
        }
        return ROBO_OK;
    case ROBO_PAINT_OP_CONTENT:
        out->kind = ROBO_PAINT_DISPLAY_CMD_CONTENT;
        out->rect = operation->boxes.content;
        return ROBO_OK;
    case ROBO_PAINT_OP_END_BOX:
        out->kind = ROBO_PAINT_DISPLAY_CMD_END_NODE;
        out->rect = operation->boxes.border;
        return ROBO_OK;
    default:
        return ROBO_ERROR;
    }
}

/* ------------------------------------------------------------------ */
/* The structural check and the build                                  */
/* ------------------------------------------------------------------ */

/** Refuse the build: empty the output list and report why. */
static robo_result_t display_refuse(robo_paint_display_list_t *out_list,
                                    robo_paint_diag_t *diag,
                                    const char *message)
{
    robo_paint_display_list_clear(out_list);
    return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_ERROR, message);
}

robo_result_t robo_paint_display_list_builder_build(robo_paint_display_list_builder_t *builder,
                                              const robo_paint_list_t *records,
                                              robo_paint_display_list_t *out_list,
                                              robo_paint_display_list_stats_t *out_stats,
                                              robo_paint_diag_t *diag)
{
    robo_paint_display_list_stats_t stats;
    robo_paint_text_pool_t *text_target;
    const robo_paint_text_pool_t *text_source;
    size_t count;
    size_t index;

    if (!builder || !records || !out_list) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_INVALID_ARGUMENT,
                               "the builder, the records and the output list are required");
    }
    /* The two pools a text run crosses between: the record list's, which holds the
     * characters the traversal read, and the output list's, which will hold the copy this
     * artifact owns. Both are borrowed for the duration of the build and neither is
     * retained -- an output list that kept a pointer to the record list's pool would be
     * exactly the dangling reference the pool exists to make impossible. */
    text_target = robo_paint_display_list_text_pool(out_list);
    text_source = robo_paint_list_text_pool(records);

    /*
     * The list is cleared first, so a caller may reuse one display list across builds and a
     * refused build hands back an **empty** list rather than the previous one -- the paint
     * pass's rule at the same boundary. It clears the list's **text pool** with it, which is
     * what makes the copy below the only characters this artifact holds: were the pool kept
     * across a build, the second build's runs would be appended after the first's and a
     * reader would be able to reach code points no command names.
     */
    robo_paint_display_list_clear(out_list);
    builder->open_count = 0u;
    memset(&stats, 0, sizeof(stats));

    count = robo_paint_list_count(records);
    stats.records = count;

    for (index = 0u; index < count; index++) {
        robo_paint_operation_t operation;
        robo_paint_display_command_t command;

        if (robo_paint_list_at(records, index, &operation) != ROBO_OK) {
            /* Unreachable: the index came from the list's own count. Reported rather than
             * skipped so a future list that can lose a record says so here. */
            return display_refuse(out_list, diag, "the record list changed under the build");
        }

        /*
         * The structural check, kind by kind. `open_count` is the number of boxes
         * currently open, so a box that nests directly inside them has depth
         * `open_count`, and a record that belongs to the innermost open box has depth
         * `open_count - 1`.
         *
         * A **text run** is neither, and the difference is the whole of 11.6's shape in
         * this file: it is a **child** of the box whose element owns the text node, so its
         * depth is one past the innermost open box and its node is a *different* object --
         * but it opens no box of its own, because a run is not one (`RoboPaintTypes.h`).
         * So it is checked as a child and the stack is left alone, and the fact that it
         * follows the box's content boundary is required exactly as it is for a nested box:
         * a run emitted before its parent's own painting had finished would be a run no
         * traversal of this build can produce.
         */
        if (operation.kind == ROBO_PAINT_OP_BEGIN_BOX) {
            if (operation.depth != builder->open_count) {
                return display_refuse(out_list, diag,
                                      "a begin-box nests where its depth says it does not");
            }
            if (display_stack_push(builder, operation.node, diag) != ROBO_OK) {
                robo_paint_display_list_clear(out_list);
                return diag ? diag->code : ROBO_OUT_OF_MEMORY;
            }
        } else {
            display_open_box_t *top;

            if (builder->open_count == 0u) {
                return display_refuse(out_list, diag, "a record belongs to no open box");
            }
            top = &builder->open[builder->open_count - 1u];
            if (operation.kind == ROBO_PAINT_OP_TEXT) {
                /* A text child: one deeper than the box that owns it, naming itself rather
                 * than that box, and inside the box's contents. Nothing is pushed, because
                 * a run has nothing to put anything in. Its depth is checked *before* the
                 * generic rule below, because that rule is what every record owned by the
                 * innermost box obeys and a run is owned a level down from it. */
                if (operation.depth != builder->open_count) {
                    return display_refuse(out_list, diag,
                                          "a text run's depth is not its parent box's plus "
                                          "one");
                }
                if (operation.node == top->node) {
                    return display_refuse(out_list, diag,
                                          "a text run names the box that owns it");
                }
                if (!top->content_seen) {
                    return display_refuse(out_list, diag,
                                          "a text run precedes its parent box's content "
                                          "boundary");
                }
                if (display_command_for(&operation, text_source, text_target, &command,
                                        diag) != ROBO_OK) {
                    return display_refuse(out_list, diag,
                                          "a text run's characters could not be copied");
                }
                if (robo_paint_display_list_append(out_list, &command, diag) != ROBO_OK) {
                    robo_paint_display_list_clear(out_list);
                    return diag ? diag->code : ROBO_ERROR;
                }
                stats.texts++;
                continue;
            }
            if (operation.depth != builder->open_count - 1u) {
                return display_refuse(out_list, diag,
                                      "a record's depth is not its innermost box's");
            }
            if (operation.node != top->node) {
                return display_refuse(out_list, diag,
                                      "a record names a node other than its innermost box's");
            }

            switch (operation.kind) {
            case ROBO_PAINT_OP_CONTENT:
                if (top->content_seen) {
                    return display_refuse(out_list, diag,
                                          "a box passes its content boundary twice");
                }
                top->content_seen = 1u;
                break;
            case ROBO_PAINT_OP_END_BOX:
                if (!top->content_seen) {
                    return display_refuse(out_list, diag,
                                          "a box closes before its content boundary");
                }
                builder->open_count--;
                break;
            default:
                /* A drawing record. It belongs to the box's own painting, which is over at
                 * its content boundary. */
                if (top->content_seen) {
                    return display_refuse(out_list, diag,
                                          "a decoration follows its box's content boundary");
                }
                if (!robo_paint_op_kind_draws(operation.kind)) {
                    return display_refuse(out_list, diag, "an operation this build cannot "
                                                          "convert");
                }
                break;
            }
        }

        if (display_command_for(&operation, text_source, text_target, &command, diag) !=
            ROBO_OK) {
            return display_refuse(out_list, diag,
                                  "an operation resolves to no display command");
        }
        if (robo_paint_display_list_append(out_list, &command, diag) != ROBO_OK) {
            robo_paint_display_list_clear(out_list);
            return diag ? diag->code : ROBO_ERROR;
        }

        /* The counters, kept beside the append so the stats describe exactly what the list
         * holds. */
        if (command.kind == ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE) {
            stats.nodes++;
            if (operation.depth > (size_t)stats.max_depth) {
                stats.max_depth = (uint32_t)operation.depth;
            }
        } else if (command.kind == ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND) {
            stats.backgrounds++;
        } else if (command.kind == ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER) {
            stats.borders++;
        }
    }

    if (builder->open_count != 0u) {
        return display_refuse(out_list, diag, "the record list does not close every box it "
                                              "opens");
    }

    stats.commands = robo_paint_display_list_count(out_list);
    stats.draws = stats.backgrounds + stats.borders + stats.texts;
    if (out_stats) {
        *out_stats = stats;
    }
    return ROBO_OK;
}
