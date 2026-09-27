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

int LayoutFcKindIsKnown(LayoutFcKind kind)
{
    return kind >= RoboLayoutFcNone &&
           kind < (LayoutFcKind)RoboLayoutFcKindCount;
}

const char *LayoutFcKindName(LayoutFcKind kind)
{
    switch (kind) {
    case RoboLayoutFcNone:
        return "none";
    case RoboLayoutFcRoot:
        return "root";
    case RoboLayoutFcBlock:
        return "block";
    case RoboLayoutFcInline:
        return "inline";
    case RoboLayoutFcFlex:
        return "flex";
    default:
        return "unknown";
    }
}

int LayoutFcKindIsBlockContainer(LayoutFcKind kind)
{
    /* ROOT is a block formatting context in CSS 2.1's own words -- the initial
     * one -- so it answers with BLOCK rather than being an exception a later pass
     * has to remember. */
    return kind == RoboLayoutFcRoot || kind == RoboLayoutFcBlock;
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

void LayoutFormattingContextInit(LayoutFormattingContext *context)
{
    if (!context) {
        return;
    }
    memset(context, 0, sizeof(*context));
}

int LayoutFormattingContextEqual(const LayoutFormattingContext *a,
                                         const LayoutFormattingContext *b)
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

int LayoutFcDisplayIsBlockContainer(CssDisplay display)
{
    CssDisplayOuter outer = MakeCssDisplayOuter(display);

    if (!CssDisplayGeneratesBox(display)) {
        return 0;
    }
    /* A block-level box with a **flow or flow-root** inner is a block container;
     * a flex container is not -- its children are flex items, and that is exactly
     * the distinction the inner column exists to carry. */
    if (outer == RoboCssDisplayOuterBlock &&
        MakeCssDisplayInner(display) != RoboCssDisplayInnerFlex) {
        return 1;
    }
    /* An atomic inline's *inner* box is one. It is a real box in CSS and not one
     * this build creates -- 10.1's builder makes one object per element -- so the
     * answer here is about the display value: "can a block-level child live
     * directly inside this?" is yes for `inline-block` and no for `inline`, and
     * that is the question the caller is asking. */
    if (outer == RoboCssDisplayOuterInline &&
        MakeCssDisplayInner(display) == RoboCssDisplayInnerFlowRoot) {
        return 1;
    }
    return 0;
}

int LayoutFcDisplayCanEstablish(CssDisplay display)
{
    if (!CssDisplayGeneratesBox(display)) {
        return 0;
    }
    /* An inner display of flow-root is a new block formatting context by
     * definition -- which is what makes `inline-block` establish one without this
     * function naming it. An inner display of flex is a new **flex** context (19.3)
     * by the same definition. */
    if (MakeCssDisplayInner(display) == RoboCssDisplayInnerFlowRoot ||
        MakeCssDisplayInner(display) == RoboCssDisplayInnerFlex) {
        return 1;
    }
    return LayoutFcDisplayIsBlockContainer(display);
}

LayoutFcKind LayoutFcEstablishes(CssDisplay display,
                                                 int hasBlockLevelChild,
                                                 int isRoot,
                                                 int isOutOfFlow,
                                                 int isFlexItem)
{
    if (!CssDisplayGeneratesBox(display)) {
        /* A display that generates no box has no contents, so there is nothing
         * for a context to format. It is also unreachable from a built tree --
         * 10.1's builder creates no box for `display: none` -- which is why the
         * answer is the absence rather than an error: a caller asking about a
         * value it read off a style is asking a legitimate question. */
        return RoboLayoutFcNone;
    }
    if (isRoot) {
        /* Whatever it computes to. The root box establishes the initial
         * formatting context, and a document whose root element is `inline` still
         * has one -- which is why this is asked before any display question. */
        return RoboLayoutFcRoot;
    }
    if (MakeCssDisplayInner(display) == RoboCssDisplayInnerFlowRoot) {
        return RoboLayoutFcBlock;
    }
    /*
     * Phase 19.3: an inner display of `flex` establishes a flex formatting context,
     * whatever the box's children are and whether or not it is positioned. The flex
     * algorithm itself is the pass's (RoboLayoutFlex.h), and this is only the fact
     * that the box owns a context the block and inline passes must not stack.
     */
    if (MakeCssDisplayInner(display) == RoboCssDisplayInnerFlex) {
        return RoboLayoutFcFlex;
    }
    /*
     * Phase 14.0: an out-of-flow block container establishes a block context of its own, before the
     * transparency question below is asked. CSS 2.1 §9.4.1 lists "absolutely positioned elements"
     * among the boxes that establish a new block formatting context, and the reason the order
     * matters here is specific: transparency says "a block-level child participates in whatever I
     * participate in", which is true of a box *in flow* because its children are laid out in its
     * parent's flow. An out-of-flow box's children are not: they are laid out inside it, after its
     * containing block has resolved, so they need a context this box owns.
     */
    if (isOutOfFlow && LayoutFcDisplayIsBlockContainer(display)) {
        return RoboLayoutFcBlock;
    }
    if (!LayoutFcDisplayIsBlockContainer(display)) {
        /* An inline box establishes nothing: it is a participant in its parent's
         * inline context. This is the one line that keeps `inline` from being
         * modelled as "a small block". */
        return RoboLayoutFcNone;
    }
    if (hasBlockLevelChild) {
        /*
         * Phase 19.3: a **flex item** establishes a block formatting context rather than
         * being transparent. CSS says every flex item establishes an independent formatting
         * context, and the reason it has to be stated here is specific: a transparent item's
         * block-level children would reach the *flex* context through the parent chain, and a
         * block-level box participating in a flex context is exactly the contradiction the
         * block pass reports.
         */
        if (isFlexItem) {
            return RoboLayoutFcBlock;
        }
        /* Transparent: its block-level children participate in whatever it
         * participates in. This is CSS 2.1's rule and it is load-bearing twice
         * over -- it is why a margin escapes a plain `<div>`, and it is why a
         * float is not contained by one. A build that answered BLOCK here would
         * make both impossible to implement correctly in 10.9 and 10.13. */
        return RoboLayoutFcNone;
    }
    /* A block container with no block-level child: only inline-level content, so
     * it establishes an inline formatting context. A container holding nothing at
     * all satisfies this vacuously, and a container holding only text satisfies it
     * because a text node is not a box (10.1) -- which is exactly the case a
     * `<p>one</p>` is, and the case 10.6's line boxes will need. */
    return RoboLayoutFcInline;
}
