/*
 * RoboLayout formatting contexts (Phase 10.2).
 *
 * A formatting context is the thing that decides *how* a set of boxes lays each
 * other out: in the block direction, one after another; or in the inline
 * direction, along a line. It is not a box and it is not a property — it is a
 * grouping, and the grouping is the answer to the only question a layout
 * algorithm can start with:
 *
 *   which boxes' layout am I coordinating, and in which direction?
 *
 * Two questions, kept apart
 *
 * The tempting model is one fact per box — "this box is a block formatting
 * context" — and it is wrong in a way that costs a later phase a rewrite. A box
 * has *two* relationships to formatting contexts and they are independent:
 *
 *   it participates in one   where its own layout decisions take place. Every
 *                            live box has exactly one, and it is the nearest
 *                            context established by an ancestor.
 *
 *   it establishes one       for its *contents*. Most boxes establish none, and
 *                            then their contents take part in whatever they take
 *                            part in. That is not a gap: it is CSS's own rule,
 *                            and it is why margins collapse *through* a plain
 *                            block box, and why a float contained by an
 *                            `inline-block` is not contained by a `div`.
 *
 * `LayoutObject` therefore carries two ids (RoboLayoutObject.h), and the
 * distinction is the reason this phase exists rather than a single boolean on
 * the kind vocabulary.
 *
 * A box whose contents are formatted by a context it did not establish is
 * *transparent* to formatting context: it is still a box with its own edges, but
 * its children look further up. Modelling it as an established block formatting
 * context would be the single most expensive thing this phase could get wrong,
 * because every box would then be a barrier and Phase 10.9 could not implement
 * margin collapsing at all.
 *
 * The four kinds, and why there is no fifth
 *
 *   NONE    no context. Not a value a live box has; it is the absence, and it is
 *           what a cleared record and an unclassified tree read as.
 *   ROOT    the initial formatting context, established by the root box and by
 *           nothing else. It has no parent, and its coordinate space is the
 *           initial containing block's.
 *   BLOCK   participants are laid out in the block direction, one after another.
 *   INLINE  participants are laid out in the inline direction, along a line.
 *
 * FLEX, GRID, TABLE, RUBY and MULTICOLUMN are deliberately **not** enumerators
 * yet, for the reason Phase 9.12 refused to ship `display: flex`: an enumerator
 * with no row behind it is a name the module can spell and cannot honour, and a
 * caller that asked for it would get a kind no algorithm produces. They join as
 * rows when their contexts exist, exactly as `display` values do.
 *
 * What establishes what
 *
 * `LayoutFcEstablishes` is the whole classification, and it is a pure
 * function of three facts: the box's computed `display`, whether the box has a
 * block-level child, and whether it is the root. The rules are CSS's:
 *
 *   the root box establishes ROOT, whatever it computes to;
 *
 *   a box whose inner display is `flow-root` establishes BLOCK — because that is
 *   what `flow-root` *is*. In this build that is `inline-block`, whose inner
 *   display 9.12 records as `flow-root` precisely so that this phase would not
 *   have to special-case it. Future `flow-root`, `flex` and `grid` rows arrive
 *   here for free;
 *
 *   a block container with no block-level child establishes INLINE — CSS 2.1
 *   §9.4.2, and the reason a `<p>` with text in it has somewhere for a line box
 *   to go in 10.6. A container that is empty, or that contains only text,
 *   satisfies this, and both are correct;
 *
 *   a block container *with* a block-level child establishes nothing: it is
 *   transparent, its block-level children find their context further up, and
 *   that is the state margin collapsing and float containment both depend on;
 *
 *   an inline box establishes nothing. It is a participant in its parent's
 *   inline context, not a context of its own — which is exactly what
 *   distinguishes it from `inline-block`, an *atomic* inline that does establish
 *   one.
 *
 * The one simplification, stated
 *
 * CSS gives a block container with *mixed* content (a block child and text, or a
 * block child and an inline child) anonymous block boxes wrapping each inline
 * run, after which the container holds only block-level children and the
 * *anonymous* boxes establish the inline contexts. This build creates no
 * anonymous boxes — that is Phase 10.3's machinery, and inventing a box here
 * would be inventing a box with no element and no style before there is anything
 * that can lay one out. So a mixed container is classified as transparent, and
 * when 10.3 introduces the anonymous boxes they will establish the inline
 * contexts and this classification will already be right about them.
 *
 * What this module is not
 *
 * Not a layout: nothing here measures anything, and no function in this header
 * takes or returns a rectangle. `LayoutFcEstablishes` answers *which kind
 * of context*, never how to lay one out. Not a CSS parser: the display value
 * arrives already computed, and the only thing read out of it is the outer/inner
 * pair 9.12 records. Not a cache: the classification is computed once per pass and
 * stored on the tree (RoboLayoutTree.h), so that a later phase can ask a question
 * after the pass without holding the pass.
 */

#ifndef ROBO_LAYOUT_FORMATTING_CONTEXT_H
#define ROBO_LAYOUT_FORMATTING_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDisplay.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Identity                                                            */
/* ------------------------------------------------------------------ */

/**
 * Tree-scoped identity of one formatting context. Never a pointer, never reused.
 *
 * Dense and 1-based within a tree, the same discipline layout object ids follow
 * (RoboLayoutTypes.h) and for the same reason: `id == index + 1` makes walking
 * the contexts a loop rather than a lookup. 0 is the absence, so a cleared id is
 * not a context.
 *
 * Contexts are *fewer* than boxes and there is no fixed ratio: a tree where every
 * box establishes one has as many contexts as boxes, and a tree of nested inline
 * boxes has one. The count is not a property to assert; it is a consequence of
 * the classification, which is why the suites assert it against the tree's own
 * shape rather than against a number.
 */
typedef uint32_t LayoutFcId;

#define RoboLayoutFcIdNone ((LayoutFcId)0u)

/* ------------------------------------------------------------------ */
/* Kind                                                                */
/* ------------------------------------------------------------------ */

/** What kind of formatting context this is. */
typedef enum LayoutFcKind {
    /** No context at all: the absence, not a kind. */
    RoboLayoutFcNone = 0,
    /** The initial formatting context, established by the root box alone. */
    RoboLayoutFcRoot = 1,
    /** Participants are laid out in the block direction, one after another. */
    RoboLayoutFcBlock = 2,
    /** Participants are laid out in the inline direction, along a line. */
    RoboLayoutFcInline = 3
} LayoutFcKind;

/** Number of defined context kinds. NONE is one of them. */
#define RoboLayoutFcKindCount 4u

/** Non-zero when @p kind is a defined kind. NONE is defined. */
int LayoutFcKindIsKnown(LayoutFcKind kind);

/** Human-readable kind name ("root", "block", "inline"); never NULL, "unknown"
 *  for an undefined enumerator. */
const char *LayoutFcKindName(LayoutFcKind kind);

/**
 * Whether a box participating in a context of @p kind is laid out in the *block*
 * direction.
 *
 * ROOT and BLOCK answer 1; INLINE answers 0; an undefined enumerator answers 0.
 * The predicate exists because it is the one fact a later block pass and a later
 * inline pass each ask first, and because stating it here keeps the two from
 * disagreeing: a build where a block pass believed ROOT was not a block context
 * would stack the root's children in the wrong direction while every other test
 * passed.
 */
int LayoutFcKindIsBlockContainer(LayoutFcKind kind);

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

/**
 * One formatting context.
 *
 * A record by value and owned by nobody: it holds no allocation, so reading one
 * out of the tree copies it and the tree is unaffected. The tree owns the array
 * it lives in (RoboLayoutTree.h).
 */
typedef struct LayoutFormattingContext {
    /** Tree-scoped identity; never NONE for a live context. */
    LayoutFcId id;
    LayoutFcKind kind;
    /**
     * The box whose *contents* this context formats.
     *
     * The owner establishes it; it is not itself a participant of it, except for
     * ROOT, whose owner has no enclosing context to participate in. Naming the
     * owner rather than the participants is what makes "whose context is this?"
     * a lookup instead of a search.
     */
    LayoutNodeId owner;
    /**
     * The context the owner participates in, or NONE for ROOT.
     *
     * Contexts therefore form a tree, and its shape is a subsequence of the box
     * tree's: every box that establishes nothing is absent from it.
     */
    LayoutFcId parent;
    /**
     * Boxes whose participation is this context.
     *
     * Counted by the pass, so a caller does not walk the tree to ask how many
     * boxes a context coordinates. The owner is counted only for ROOT (see the
     * field above).
     */
    size_t participants;
    /** Nesting depth: 0 for ROOT, the parent's depth plus one otherwise. */
    uint32_t depth;
} LayoutFormattingContext;

/** Clear @p context to "no context", zero participants and zero depth. Safe on
 *  NULL. */
void LayoutFormattingContextInit(LayoutFormattingContext *context);

/** Field-by-field equality, including identity. Safe on NULL. */
int LayoutFormattingContextEqual(const LayoutFormattingContext *a,
                                         const LayoutFormattingContext *b);

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

/**
 * What context @p display establishes for its contents.
 *
 * The whole of this phase's CSS, in one pure function with no tree, no pass and
 * no allocation, so that the rules can be asserted as a table rather than
 * inferred from a built document. The three inputs are the three facts the rules
 * are stated in terms of:
 *
 *   @p display                 the box's *computed* display — never parsed here,
 *                              never a spelling, never an element's tag name.
 *   @p hasBlockLevelChild   whether any element child's computed display is
 *                              block-level. Meaningful only for a block
 *                              container: an inline box cannot have a block-level
 *                              child that matters, and a box with no element
 *                              children has none, which is what makes a `<p>`
 *                              holding nothing but text establish INLINE.
 *   @p isRoot                 whether this is the tree's root box, which
 *                              establishes ROOT whatever it computes to.
 *
 * @return RoboLayoutFcRoot, RoboLayoutFcBlock, RoboLayoutFcInline, or
 *         RoboLayoutFcNone when the box establishes nothing — including for a
 *         display that generates no box at all, which cannot have a context
 *         because it cannot have contents.
 */
LayoutFcKind LayoutFcEstablishes(CssDisplay display,
                                                int hasBlockLevelChild,
                                                int isRoot);

/**
 * Whether a box with @p display could *ever* establish a context in this build.
 *
 * Not the same question as @ref LayoutFcEstablishes, and the difference is
 * the reason it is here: an `inline` box answers 0 for reasons that will never
 * change, while a block container answers 1 and then answers NONE for a
 * particular child composition. A caller diagnosing "why did nothing establish a
 * context here?" needs to tell those apart, and a caller that only had the kind
 * would have to re-derive the whole rule to do it.
 */
int LayoutFcDisplayCanEstablish(CssDisplay display);

/**
 * Whether a box with @p display is a *block container* — the box type that can
 * establish an inline context and that a block-level child participates in.
 *
 * True for `block` (an outer block with a flow inner), false for `inline` (an
 * inline box is a participant, not a container) and true for `inline-block`,
 * whose inner box is the container. This is a fact about the display value rather
 * than about the context it establishes, which is why it is a separate door: the
 * IFC rule and the block-container question are asked about different boxes at
 * different times (the inner box of an `inline-block` is a block container that
 * never appears as a box in this build).
 */
int LayoutFcDisplayIsBlockContainer(CssDisplay display);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_FORMATTING_CONTEXT_H */
