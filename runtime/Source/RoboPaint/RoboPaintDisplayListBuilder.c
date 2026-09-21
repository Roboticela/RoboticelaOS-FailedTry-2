/*
 * RoboDisplay list builder (Phase 11.3–11.8) — see RoboPaintDisplayListBuilder.h.
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
 * **Since 11.8 there is a second stack beside it**, of the clip scopes currently open. It is
 * the one structural fact the box stack cannot carry: a box's contents are bounded by the
 * clip that box put in force, so a command inside an open scope must carry that scope's
 * boundary rather than any boundary of its own -- and "what is in force here?" is answered by
 * the innermost push, which is a fact about the *sequence* rather than about the box. The two
 * stacks move together (a scope opens after a box's content boundary and closes before its end
 * marker) but they are not the same stack, and merging them would mean a box that opens no
 * scope still had to carry one.
 *
 * Nothing here performs layout or touches the tree: the records are read through the paint
 * list's `const` doors and the conversion uses only what a record already carries (plus
 * `robo_paint_operation_side_band`, the one derivation the record leaves to its consumer).
 */

#include "RoboPaint/RoboPaintDisplayListBuilder.h"

#include "RoboPaint/RoboPaintClip.h"
#include "RoboPaint/RoboPaintOpacity.h"
#include "RoboPaint/RoboPaintOperation.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/**
 * One open box: its node id, whether its content boundary has passed, and which of the two
 * scopes it has open.
 *
 * **Two flags rather than one**, because a box can open both and they are two different
 * facts: `scope_open` is the clip scope around its *contents* (11.8) and `opacity_open` is
 * the opacity scope around the box *itself* (11.9). They also live at different points in
 * the box's life -- the clip pair opens after the content boundary and the opacity pair
 * opens before the box's first decoration -- so a single flag would make one of the two
 * checks read the other's state.
 */
typedef struct display_open_box {
    robo_layout_node_id_t node;
    unsigned char content_seen;
    unsigned char scope_open;
    unsigned char opacity_open;
    /** Whether anything the box draws for **itself** has been emitted yet. */
    unsigned char painted;
} display_open_box_t;

struct robo_paint_display_list_builder {
    display_open_box_t *open;
    size_t open_count;
    size_t open_capacity;
    /** The clip scopes currently open, innermost last. */
    robo_paint_clip_stack_t *clips;
    /** The opacity scopes currently open, innermost last. */
    robo_paint_opacity_stack_t *opacities;
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
    builder->open[builder->open_count].scope_open = 0u;
    builder->open[builder->open_count].opacity_open = 0u;
    builder->open[builder->open_count].painted = 0u;
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
    builder->clips = NULL;
    builder->opacities = NULL;
    if (robo_paint_clip_stack_create(&builder->clips, NULL) != ROBO_OK) {
        robo_free(builder);
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_OUT_OF_MEMORY,
                               "could not allocate the builder's clip scope stack");
    }
    if (robo_paint_opacity_stack_create(&builder->opacities, NULL) != ROBO_OK) {
        robo_paint_clip_stack_destroy(builder->clips);
        robo_free(builder);
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_OUT_OF_MEMORY,
                               "could not allocate the builder's opacity scope stack");
    }
    *out_builder = builder;
    return ROBO_OK;
}

void robo_paint_display_list_builder_destroy(robo_paint_display_list_builder_t *builder)
{
    if (!builder) {
        return;
    }
    robo_paint_clip_stack_destroy(builder->clips);
    robo_paint_opacity_stack_destroy(builder->opacities);
    robo_free(builder->open);
    robo_free(builder);
}

void robo_paint_display_list_builder_reset(robo_paint_display_list_builder_t *builder)
{
    if (!builder) {
        return;
    }
    /* The capacity survives: a builder reused per frame reuses its stack -- and so does a
     * scope that was open when a build was refused, which is why **both** scope stacks are
     * cleared here as well as at the start of every build. A second stack that were left
     * holding a scope would make the next build's first drawing disagree with a strength
     * nothing in that build had established. */
    builder->open_count = 0u;
    robo_paint_clip_stack_reset(builder->clips);
    robo_paint_opacity_stack_reset(builder->opacities);
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
    /*
     * Both states in force carry across for **every** kind, scope kinds included -- a push
     * command's own strength is the strength it establishes, and the pair's second half carries
     * the same one so that a consumer can check it against the first. See the scope comments in
     * this file for what each of the two is checked against.
     */
    out->clip = operation->clip;
    out->opacity = operation->opacity;

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
    case ROBO_PAINT_OP_IMAGE:
        /*
         * The image commands (11.7), and the one derivation this build performs for them is the
         * **destination rectangle**: an image record carries the box's three rectangles and this
         * artifact carries one, so the content box is what survives -- because an image *is* its
         * element's content, and the content box is where content goes. That is the same kind of
         * conversion the border band above performs on a side, and it is done once here so that a
         * rasteriser never has to choose between three rectangles.
         *
         * The payload travels as it is: the resource, the source rectangle in image pixels and
         * the resource's facts are all values, and none of them is derived from anything above.
         * The command this produces borrows the resource; the **list** takes the reference that
         * keeps it alive (`robo_paint_display_list_append`), which is what lets the record list be
         * cleared or destroyed while this list keeps drawing.
         */
        out->kind = ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE;
        out->rect = operation->boxes.content;
        out->image = operation->image;
        return ROBO_OK;
    case ROBO_PAINT_OP_CONTENT:
        out->kind = ROBO_PAINT_DISPLAY_CMD_CONTENT;
        out->rect = operation->boxes.content;
        return ROBO_OK;
    case ROBO_PAINT_OP_PUSH_CLIP:
    case ROBO_PAINT_OP_POP_CLIP:
        out->kind = operation->kind == ROBO_PAINT_OP_PUSH_CLIP
                            ? ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP
                            : ROBO_PAINT_DISPLAY_CMD_POP_CLIP;
        /*
         * The rectangle a clip command acts on is the box's **padding box**: the boundary the
         * box's own `overflow` resolves to, and the answer to "where did this scope come from?"
         * when a dump is being read. It is deliberately *not* the effective clip's rectangle,
         * and the reason is that the effective rectangle does not always exist: one bounded
         * axis is a legal document (`overflow-x: hidden` beside `overflow-y: visible`), so
         * `robo_paint_clip_rect` refuses exactly the case where a reader would most like a
         * rectangle and a command that answered anyway would be inventing one. The boundary
         * itself travels in the `clip` field, where the axis split survives.
         */
        out->rect = operation->boxes.padding;
        return ROBO_OK;
    case ROBO_PAINT_OP_PUSH_OPACITY:
    case ROBO_PAINT_OP_POP_OPACITY:
        out->kind = operation->kind == ROBO_PAINT_OP_PUSH_OPACITY
                            ? ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY
                            : ROBO_PAINT_DISPLAY_CMD_POP_OPACITY;
        /*
         * The rectangle is the box's **border box**, and it is the same box the clip pair names
         * for the *other* of its two rectangles -- deliberately. A clip command's rectangle is
         * the padding box because that is the boundary the box's overflow resolves to ("where
         * did this scope come from?"); an opacity command's is the border box because the
         * scope belongs to the box *itself*, decorations included, and the border box is the
         * whole of what it tints. A reader comparing the two commands of one box's two scopes
         * sees them name its two different rectangles, and each name is the true answer to its
         * own question.
         */
        out->rect = operation->boxes.border;
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

/*
 * The **opacity scope** (11.9), checked before the clip's because the two pairs sit at
 * different points in a box's life and each check depends on where its own pair sits.
 *
 * **Why the pair is here and not after the content boundary.** A clip belongs to a box's
 * *contents* -- CSS paints the box's own background under its parent's boundary -- so the clip
 * pair opens when the contents begin. An opacity belongs to the box *itself*: CSS paints the
 * box's own background and borders at its own `opacity`, so the pair opens before the box's
 * first decoration. That is why the push requires `!painted && !content_seen` while the clip's
 * push requires `content_seen`, and why the two checks are near mirror images of each other
 * rather than one rule with a flag.
 *
 * Three things are checked on a push, and each is a way a list can lie about the document it
 * came from: the scope names the innermost **open** box and sits at that box's depth (so the
 * box it belongs to is the box it names, not a child that happens to be open); it establishes
 * a strength **different** from the one in force (a scope that establishes nothing is a command
 * a consumer could only skip); and it does not *raise* the strength (opacity composes by
 * multiplication, so a scope an ancestor reduced can only reduce further -- a push that raised
 * it would be a box whose `opacity` cancelled an ancestor's, which CSS has no way to express).
 *
 * The pop is checked against the innermost open scope rather than against a value the pop
 * carries alone, so the pair is provably paired: a pop with nothing open, a pop that closes a
 * scope another box opened, and a pop whose strength is not the one its push carried are three
 * separate refusals rather than one that happens to catch all three.
 */
static robo_result_t display_opacity_scopes(robo_paint_display_list_builder_t *builder,
                                            const robo_paint_operation_t *operation,
                                            robo_paint_display_list_stats_t *stats,
                                            const char **refusal)
{
    display_open_box_t *top;
    robo_paint_opacity_t in_force;

    *refusal = NULL;
    if (builder->open_count == 0u) {
        *refusal = "an opacity scope belongs to no open box";
        return ROBO_ERROR;
    }
    top = &builder->open[builder->open_count - 1u];
    if (operation->node != top->node || operation->depth != builder->open_count - 1u) {
        *refusal = "an opacity scope names a box other than the innermost open one";
        return ROBO_ERROR;
    }
    robo_paint_opacity_stack_current(builder->opacities, &in_force);

    if (operation->kind == ROBO_PAINT_OP_PUSH_OPACITY) {
        if (top->opacity_open) {
            *refusal = "a box opens two opacity scopes";
            return ROBO_ERROR;
        }
        if (top->painted || top->content_seen) {
            *refusal = "an opacity scope opens after the box it belongs to has begun drawing";
            return ROBO_ERROR;
        }
        if (robo_paint_opacity_equal(&operation->opacity, &in_force)) {
            *refusal = "an opacity scope establishes nothing new";
            return ROBO_ERROR;
        }
        if (robo_paint_opacity_value(&operation->opacity) > robo_paint_opacity_value(&in_force)) {
            *refusal = "an opacity scope is stronger than the strength it is inside";
            return ROBO_ERROR;
        }
        if (stats != NULL) {
            stats->opacity_scopes++;
        }
        return ROBO_OK;
    }

    if (!top->opacity_open) {
        *refusal = "a box closes an opacity scope it did not open";
        return ROBO_ERROR;
    }
    if (!robo_paint_opacity_equal(&operation->opacity, &in_force)) {
        *refusal = "an opacity scope closes with a strength other than the one it opened";
        return ROBO_ERROR;
    }
    return ROBO_OK;
}

/**
 * Non-zero when @p narrow is at least as tight as @p outer on **every axis @p outer bounds**.
 *
 * The one property of a nested scope that can be checked without geometry: a scope is entered
 * to constrain the drawing inside it, so a push whose interval is *wider* than the boundary
 * already in force on an axis is a record the traversal cannot produce -- it would be a box
 * whose `overflow` freed an axis an ancestor had bounded, which CSS has no way to express and
 * this build has no way to reach. An axis @p outer leaves open constrains nothing, so @p narrow
 * is free to bound it or not.
 *
 * The containment is per axis and inclusive at both ends: an interval equal to the enclosing one
 * on that axis narrowing the *other* axis is a real scope (a document that hides `y` inside one
 * that hides `x`), and refusing it would refuse a document the traversal does produce. The
 * equal-on-both-axes case is refused by the caller, because a scope that changes nothing is a
 * command a consumer can only skip.
 *
 * Two cases that read like violations and are not, both of which cost a debug cycle to get right:
 *
 *   **an inset bound**   `outer = [0, 200]` with `narrow = [20, 200]` is the *ordinary* case, a
 *                        child box inset from its parent's edge. Containment is
 *                        `outer.start <= narrow.start && narrow.end <= outer.end`; the spelled
 *                        `narrow.start > outer.start` is the opposite predicate, and `20 > 0` is
 *                        exactly what an inset child produces.
 *
 *   **an empty narrow**  a scope whose intersection came out disjoint is canonicalized to
 *                        `[start, start]` -- see `paint_clip_intersect_axis` -- and a zero-length
 *                        interval at `start` sits *outside* an outer `[0, 200]`'s start, so the
 *                        containment test throws it out. It must not: an empty scope bounds
 *                        nothing, so it cannot bound too much. It is accepted here and its
 *                        emptiness does the suppressing downstream.
 */
static int display_clip_narrows(const robo_paint_clip_t *narrow,
                                const robo_paint_clip_t *outer)
{
    if (robo_paint_clip_is_empty(narrow)) {
        return 1;
    }
    if (robo_paint_clip_axis_is_active(&outer->horizontal)) {
        if (!robo_paint_clip_axis_is_active(&narrow->horizontal)) {
            return 0;
        }
        if (narrow->horizontal.start < outer->horizontal.start ||
            narrow->horizontal.end > outer->horizontal.end) {
            return 0;
        }
    }
    if (robo_paint_clip_axis_is_active(&outer->vertical)) {
        if (!robo_paint_clip_axis_is_active(&narrow->vertical)) {
            return 0;
        }
        if (narrow->vertical.start < outer->vertical.start ||
            narrow->vertical.end > outer->vertical.end) {
            return 0;
        }
    }
    return 1;
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
    robo_paint_clip_stack_reset(builder->clips);
    robo_paint_opacity_stack_reset(builder->opacities);
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
         * The **opacity scope** (11.9), handled before the clip's -- see `display_opacity_scopes`
         * for why the two pairs sit at different points in a box's life -- and before the box
         * chain for the reason a scope is not a box: it opens and closes around one box's own
         * painting and contents, and the box that declared the opacity is the innermost open box
         * in both cases.
         *
         * The push's strength is pushed onto the builder's opacity stack here, at the push, and
         * the pop's is compared against it at the pop -- so `in_force` for a drawing the box
         * makes is the box's own effective opacity rather than an ancestor's, which is the same
         * arrangement the clip stack has one field along.
         */
        if (robo_paint_op_kind_is_opacity(operation.kind)) {
            const char *refusal = NULL;
            robo_result_t scoped = display_opacity_scopes(builder, &operation, &stats, &refusal);

            if (scoped != ROBO_OK) {
                return display_refuse(out_list, diag, refusal);
            }
            if (operation.kind == ROBO_PAINT_OP_PUSH_OPACITY) {
                if (robo_paint_opacity_stack_push(builder->opacities, &operation.opacity, diag) !=
                    ROBO_OK) {
                    robo_paint_display_list_clear(out_list);
                    return diag ? diag->code : ROBO_OUT_OF_MEMORY;
                }
                builder->open[builder->open_count - 1u].opacity_open = 1u;
            } else {
                (void)robo_paint_opacity_stack_pop(builder->opacities, NULL);
                builder->open[builder->open_count - 1u].opacity_open = 0u;
            }
            if (display_command_for(&operation, text_source, text_target, &command, diag) !=
                ROBO_OK) {
                return display_refuse(out_list, diag,
                                      "an opacity scope resolves to no display command");
            }
            if (robo_paint_display_list_append(out_list, &command, diag) != ROBO_OK) {
                robo_paint_display_list_clear(out_list);
                return diag ? diag->code : ROBO_ERROR;
            }
            continue;
        }

        /*
         * The **clip scope** (11.8), handled before the box chain because a scope is not a
         * box: it opens and closes *around* one box's contents, and the box that declared the
         * overflow is the innermost open box in both cases.
         *
         * Both halves name that box and carry its own depth, exactly as the box's `CONTENT`
         * does, so a scope can be checked against the box it belongs to rather than trusted:
         * a scope emitted between two of a box's own records, or after its `END_BOX`, or inside
         * a *child* that happens to be open, is a sequence no traversal of this build produces
         * and is refused here.
         *
         * The push is then checked against what is already in force -- a scope that establishes
         * nothing, or one *wider* than the boundary it is inside, is refused (the rule the
         * in-scope check below then relies on) -- and the pop against the innermost open scope,
         * so the pair is provably paired: a pop with no scope, a pop that closes a scope another
         * box opened, and a pop whose boundary is not the one its push carried are three
         * separate refusals rather than one that happens to catch all three.
         */
        if (robo_paint_op_kind_is_clip(operation.kind)) {
            display_open_box_t *top;
            robo_paint_clip_t in_force;

            if (builder->open_count == 0u) {
                return display_refuse(out_list, diag, "a clip scope belongs to no open box");
            }
            top = &builder->open[builder->open_count - 1u];
            if (operation.node != top->node || operation.depth != builder->open_count - 1u) {
                return display_refuse(out_list, diag,
                                      "a clip scope names a box other than the innermost "
                                      "open one");
            }
            if (!top->content_seen) {
                return display_refuse(out_list, diag,
                                      "a clip scope precedes its box's content boundary");
            }
            robo_paint_clip_stack_current(builder->clips, &in_force);
            if (operation.kind == ROBO_PAINT_OP_PUSH_CLIP) {
                if (top->scope_open) {
                    return display_refuse(out_list, diag, "a box opens two clip scopes");
                }
                if (robo_paint_clip_equal(&operation.clip, &in_force)) {
                    return display_refuse(out_list, diag,
                                          "a clip scope establishes nothing new");
                }
                if (!display_clip_narrows(&operation.clip, &in_force)) {
                    return display_refuse(out_list, diag,
                                          "a clip scope is wider than the boundary it is "
                                          "inside");
                }
                if (robo_paint_clip_stack_push(builder->clips, &operation.clip, diag) != ROBO_OK) {
                    robo_paint_display_list_clear(out_list);
                    return diag ? diag->code : ROBO_OUT_OF_MEMORY;
                }
                top->scope_open = 1u;
                stats.clip_scopes++;
            } else {
                if (!top->scope_open) {
                    return display_refuse(out_list, diag,
                                          "a box closes a clip scope it did not open");
                }
                if (!robo_paint_clip_equal(&operation.clip, &in_force)) {
                    return display_refuse(out_list, diag,
                                          "a clip scope closes with a boundary other than the "
                                          "one it opened");
                }
                (void)robo_paint_clip_stack_pop(builder->clips, NULL);
                top->scope_open = 0u;
            }
            if (display_command_for(&operation, text_source, text_target, &command, diag) !=
                ROBO_OK) {
                return display_refuse(out_list, diag,
                                      "a clip scope resolves to no display command");
            }
            if (robo_paint_display_list_append(out_list, &command, diag) != ROBO_OK) {
                robo_paint_display_list_clear(out_list);
                return diag ? diag->code : ROBO_ERROR;
            }
            continue;
        }

        /*
         * Everything else is inside whatever scope is open, and **must carry its boundary**.
         * This is the rule the clip commands exist to make checkable: without it a push and a
         * pop would be two structural commands a consumer could only skip, and the boundary a
         * drawing is bounded by would still be a field nothing cross-checks.
         *
         * The check is made *only* when a scope is open, and that asymmetry is deliberate: a
         * list the traversal produced always has a scope open whenever a clip is in force, so
         * the check catches every disagreement that can occur -- while a hand-built list with
         * no clip records at all keeps working exactly as it did before this phase, its own
         * commands' boundaries being the only statement of clipping it has. Requiring a scope
         * for every bounded command would have refused a list that is merely *literal* about
         * its clip rather than structured by one.
         */
        if (!robo_paint_clip_stack_is_empty(builder->clips)) {
            robo_paint_clip_t in_force;

            robo_paint_clip_stack_current(builder->clips, &in_force);
            if (!robo_paint_clip_equal(&operation.clip, &in_force)) {
                return display_refuse(out_list, diag,
                                      "a command inside a clip scope carries a different "
                                      "boundary");
            }
        }
        /*
         * And the same rule for the strength in force (11.9), with the same asymmetry and for
         * the same reason: a list the traversal produced always has an opacity scope open
         * whenever a strength other than full is in force, so the check catches every
         * disagreement that can occur -- while a hand-built list with no opacity records at all
         * keeps working, its own commands' strengths being the only statement of opacity it has.
         */
        if (!robo_paint_opacity_stack_is_empty(builder->opacities)) {
            robo_paint_opacity_t in_force;

            robo_paint_opacity_stack_current(builder->opacities, &in_force);
            if (!robo_paint_opacity_equal(&operation.opacity, &in_force)) {
                return display_refuse(out_list, diag,
                                      "a command inside an opacity scope carries a "
                                      "different strength");
            }
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
 *
 * An **image** (11.7) takes the ordinary path below -- the innermost open box's depth and
 * node, and the rule that it precedes that box's content boundary -- and the check is not
 * special-cased for it on purpose: those are exactly the facts a replaced element's drawing has,
 * so a site that carved out an exception for images would be stating the rule twice and
 * differently. It is the *traversal* that places the record (after the box's decorations, before
 * its `CONTENT`), and this file's job is to refuse a sequence that does not hold. The same is
 * true of the opacity scope's `painted` flag below: an image is a drawing the box made for
 * itself, so a `PUSH_OPACITY` after one is refused there automatically.
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
                /* A box's opacity scope closes before its marker -- the traversal emits the
                 * pop and then the `END_BOX` -- so a box that closes while one is still open
                 * is a box whose own strength leaked onto whatever follows it. The box stack
                 * alone would not catch it: the frame is about to be discarded, and the scope
                 * it left open is a command in the list with no matching pop until the end of
                 * the build, which is a different and later failure. */
                if (top->opacity_open) {
                    return display_refuse(out_list, diag,
                                          "a box closes with an opacity scope still open");
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
                /* The box has begun drawing for itself, which is what an opacity push may not
                 * follow -- see `display_opacity_scopes`. */
                top->painted = 1u;
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
        } else if (command.kind == ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE) {
            stats.images++;
        }
    }

    if (builder->open_count != 0u) {
        return display_refuse(out_list, diag, "the record list does not close every box it "
                                              "opens");
    }
    /*
     * A scope left open at the end of a build is checked **here** as well as by the box stack,
     * and it is not the same failure: a box stack that came back to zero with a scope still
     * open would be a list whose last command is inside a boundary nothing closes -- a
     * consumer replaying it would end with a clip in force it never popped. The traversal
     * cannot produce it (a scope closes with the box that opened it) and a hand-built list
     * can, so it is reported rather than assumed.
     */
    if (!robo_paint_clip_stack_is_empty(builder->clips)) {
        return display_refuse(out_list, diag, "the record list does not close every clip scope "
                                              "it opens");
    }
    /* The same check for the strength (11.9), and here the failure is sharper than the clip's:
     * a strength left in force would make every drawing after it faint for a reason no command
     * in the list states. The per-box check above catches a box that closed with one open; this
     * one catches a list whose box stack balanced while a scope did not. */
    if (!robo_paint_opacity_stack_is_empty(builder->opacities)) {
        return display_refuse(out_list, diag, "the record list does not close every opacity "
                                              "scope it opens");
    }

    stats.commands = robo_paint_display_list_count(out_list);
    /* Every drawing kind the vocabulary has, added up in the one place the total is stated --
     * images included since 11.7, so `draws` stays the number a reader can check against
     * `commands - skips` whatever kinds the next phase adds. */
    stats.draws = stats.backgrounds + stats.borders + stats.texts + stats.images;
    if (out_stats) {
        *out_stats = stats;
    }
    return ROBO_OK;
}
