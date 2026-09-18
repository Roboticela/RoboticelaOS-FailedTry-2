/*
 * RoboLayout formatting contexts (Phase 10.2) — see RoboLayoutFormattingContext.h.
 *
 * Two things live here and nothing else: the kind vocabulary, and the
 * classification. The classification is a pure function of a display value, one
 * boolean and one flag, which is the whole reason it is not buried in the pass —
 * a rule that can only be observed by building a document and walking it is a
 * rule whose table cannot be asserted, and every entry of that table is a claim
 * about CSS.
 *
 * The order of the questions inside it is the logic, not an implementation
 * detail. `generates a box` comes first because a display that generates nothing
 * cannot have contents; `is the root` comes next because the root establishes
 * ROOT whatever else is true of it; `flow-root` comes next because an inner
 * display of flow-root *is* a block formatting context and asking about children
 * first would replace a CSS fact with a structural one; and the child composition
 * is asked last because it is the only rule that is about the tree rather than
 * about the value.
 */

#include "RoboLayout/RoboLayoutFormattingContext.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Kind                                                                */
/* ------------------------------------------------------------------ */

int robo_layout_fc_kind_is_known(robo_layout_fc_kind_t kind)
{
    return kind >= ROBO_LAYOUT_FC_NONE &&
           kind < (robo_layout_fc_kind_t)ROBO_LAYOUT_FC_KIND_COUNT;
}

const char *robo_layout_fc_kind_name(robo_layout_fc_kind_t kind)
{
    switch (kind) {
    case ROBO_LAYOUT_FC_NONE:
        return "none";
    case ROBO_LAYOUT_FC_ROOT:
        return "root";
    case ROBO_LAYOUT_FC_BLOCK:
        return "block";
    case ROBO_LAYOUT_FC_INLINE:
        return "inline";
    default:
        return "unknown";
    }
}

int robo_layout_fc_kind_is_block_container(robo_layout_fc_kind_t kind)
{
    /* ROOT is a block formatting context in CSS 2.1's own words -- the initial
     * one -- so it answers with BLOCK rather than being an exception a later pass
     * has to remember. */
    return kind == ROBO_LAYOUT_FC_ROOT || kind == ROBO_LAYOUT_FC_BLOCK;
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

void robo_layout_formatting_context_init(robo_layout_formatting_context_t *context)
{
    if (!context) {
        return;
    }
    memset(context, 0, sizeof(*context));
}

int robo_layout_formatting_context_equal(const robo_layout_formatting_context_t *a,
                                         const robo_layout_formatting_context_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    /* Identity is part of equality, deliberately: two contexts with the same kind,
     * owner and parent are still two contexts, and a comparison that ignored the
     * id would report a classification rebuilt into a different tree as unchanged.
     */
    return a->id == b->id && a->kind == b->kind && a->owner == b->owner &&
           a->parent == b->parent && a->participants == b->participants &&
           a->depth == b->depth;
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

int robo_layout_fc_display_is_block_container(robo_css_display_t display)
{
    robo_css_display_outer_t outer = robo_css_display_outer(display);

    if (!robo_css_display_generates_box(display)) {
        return 0;
    }
    /* A block-level box with a flow or flow-root inner *is* a block container. */
    if (outer == ROBO_CSS_DISPLAY_OUTER_BLOCK) {
        return 1;
    }
    /* An atomic inline's *inner* box is one. It is a real box in CSS and not one
     * this build creates -- 10.1's builder makes one object per element -- so the
     * answer here is about the display value: "can a block-level child live
     * directly inside this?" is yes for `inline-block` and no for `inline`, and
     * that is the question the caller is asking. */
    if (outer == ROBO_CSS_DISPLAY_OUTER_INLINE &&
        robo_css_display_inner(display) == ROBO_CSS_DISPLAY_INNER_FLOW_ROOT) {
        return 1;
    }
    return 0;
}

int robo_layout_fc_display_can_establish(robo_css_display_t display)
{
    if (!robo_css_display_generates_box(display)) {
        return 0;
    }
    /* An inner display of flow-root is a new block formatting context by
     * definition -- which is what makes `inline-block` establish one without this
     * function naming it. */
    if (robo_css_display_inner(display) == ROBO_CSS_DISPLAY_INNER_FLOW_ROOT) {
        return 1;
    }
    return robo_layout_fc_display_is_block_container(display);
}

robo_layout_fc_kind_t robo_layout_fc_establishes(robo_css_display_t display,
                                                 int has_block_level_child,
                                                 int is_root)
{
    if (!robo_css_display_generates_box(display)) {
        /* A display that generates no box has no contents, so there is nothing
         * for a context to format. It is also unreachable from a built tree --
         * 10.1's builder creates no box for `display: none` -- which is why the
         * answer is the absence rather than an error: a caller asking about a
         * value it read off a style is asking a legitimate question. */
        return ROBO_LAYOUT_FC_NONE;
    }
    if (is_root) {
        /* Whatever it computes to. The root box establishes the initial
         * formatting context, and a document whose root element is `inline` still
         * has one -- which is why this is asked before any display question. */
        return ROBO_LAYOUT_FC_ROOT;
    }
    if (robo_css_display_inner(display) == ROBO_CSS_DISPLAY_INNER_FLOW_ROOT) {
        return ROBO_LAYOUT_FC_BLOCK;
    }
    if (!robo_layout_fc_display_is_block_container(display)) {
        /* An inline box establishes nothing: it is a participant in its parent's
         * inline context. This is the one line that keeps `inline` from being
         * modelled as "a small block". */
        return ROBO_LAYOUT_FC_NONE;
    }
    if (has_block_level_child) {
        /* Transparent: its block-level children participate in whatever it
         * participates in. This is CSS 2.1's rule and it is load-bearing twice
         * over -- it is why a margin escapes a plain `<div>`, and it is why a
         * float is not contained by one. A build that answered BLOCK here would
         * make both impossible to implement correctly in 10.9 and 10.13. */
        return ROBO_LAYOUT_FC_NONE;
    }
    /* A block container with no block-level child: only inline-level content, so
     * it establishes an inline formatting context. A container holding nothing at
     * all satisfies this vacuously, and a container holding only text satisfies it
     * because a text node is not a box (10.1) -- which is exactly the case a
     * `<p>one</p>` is, and the case 10.6's line boxes will need. */
    return ROBO_LAYOUT_FC_INLINE;
}
