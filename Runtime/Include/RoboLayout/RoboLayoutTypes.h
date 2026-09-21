/*
 * RoboLayout identity and vocabulary (Phase 10.1).
 *
 * Object identity is a *tree-scoped* dense id, not a pointer and not a DOM id:
 *
 *   - ids are 1-based and dense, so `id == index + 1` holds for the whole life
 *     of a tree, which is what makes walking one a loop rather than a lookup.
 *   - ids restart when a tree is cleared, exactly as RoboHTML's node ids do, and
 *     for the same reason: a tree is rebuilt wholesale and a stale id must not
 *     alias whatever box now occupies that slot.
 *   - a layout object is *not* a DOM node and holds no DOM node. It holds a
 *     `HtmlNodeHandle` -- an inert token RoboHTML defined -- so a caller
 *     that has a box can ask the DOM about the element it came from, and a caller
 *     that has an element can ask the tree for its box.
 *
 * The object vocabulary is deliberately one entry wide. 10.1 builds *boxes*: an
 * element that generates one. Block boxes, inline boxes, line boxes and text are
 * later goals, and shipping enumerators for them now would be claiming a
 * distinction the tree cannot yet make.
 */

#ifndef ROBO_LAYOUT_TYPES_H
#define ROBO_LAYOUT_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A record declared with this macro is in its empty state.
 *
 * RoboHTML and RoboCSS use the same idea, and for the same reason: the empty
 * state is the all-zero byte pattern *by design*, so declaring with the macro and
 * declaring then calling the matching init function produce the same record. The
 * one rule that goes with it is the one RoboCSSTypes.h states: a record must be
 * cleared before any call that may release what it already holds.
 */
#define RoboLayoutCleared { 0 }

/** Tree-scoped identity of one layout object. Never a pointer, never reused. */
typedef uint32_t LayoutNodeId;

#define RoboLayoutNodeIdNone ((LayoutNodeId)0u)

/**
 * What kind of layout object this is.
 *
 * 10.1 shipped one value, and the point then was that it knew how to say "this
 * element generates a box" and nothing about what *kind* of box it is -- the
 * formatting contexts that answer that are 10.2's.
 *
 * 10.5 adds the second, and it is not a box: a **text object** is what a text
 * node generates (RoboLayoutText.h). It is a layout object because it
 * participates in layout -- it is placed along a line, it advances a cursor, it
 * has a position and a size -- and it is not a box because it has no element, no
 * children and no `display` declaration of its own. Collapsing the two would mean
 * either giving text an element it does not have or making the passes ask "is
 * this one of the special ones?" in a dozen places.
 *
 * The vocabulary is deliberately still small, and the third entry is still
 * absent: an **anonymous** box (the wrapper a mixed container needs) is a later
 * goal's, and it would be the first kind with no DOM node behind it at all --
 * which is why it is 10.3's and 10.4's machinery rather than something to claim
 * here.
 */
typedef enum LayoutObjectKind {
    RoboLayoutObjectNone = 0,
    /** An element that generates a box (any display except `none`). */
    RoboLayoutObjectBox = 1,
    /** A text node's content, placed as a run (10.5). */
    RoboLayoutObjectText = 2
} LayoutObjectKind;

/** Number of defined object kinds. */
#define RoboLayoutObjectKindCount 3u

/** Non-zero if @p kind is a defined object kind. */
int LayoutObjectKindIsKnown(LayoutObjectKind kind);

/** Human-readable kind name; never NULL; "unknown" when undefined. */
const char *LayoutObjectKindName(LayoutObjectKind kind);

/*
 * Diagnostic domains. The same discipline RoboHTML and RoboCSS use: a call
 * returns a plain Result and an optional caller-owned record refines the
 * failure with a domain and a message. There is no global mutable error state.
 */
typedef enum LayoutDiagnosticDomain {
    RoboLayoutDomainNone = 0,
    /** Module lifecycle: not initialized, already initialized. */
    RoboLayoutDomainModule = 1,
    /** Tree: creation, capacity, membership, lookup. */
    RoboLayoutDomainTree = 2,
    /** Objects: links, association and the read side. */
    RoboLayoutDomainObject = 3,
    /** Constraints: availability, bounds, clamping. */
    RoboLayoutDomainConstraints = 4,
    /** Results: the geometry a later goal fills in. */
    RoboLayoutDomainResult = 5,
    /** Context: one layout pass and its counters. */
    RoboLayoutDomainContext = 6,
    /** The DOM-to-tree builder. */
    RoboLayoutDomainBuilder = 7,
    /** The style source seam: what a consumer answers about one element. */
    RoboLayoutDomainStyle = 8,
    /** The DOM + computed-style bridge. */
    RoboLayoutDomainDom = 9,
    /** The geometry vocabulary (borrowed from RoboGraphics). */
    RoboLayoutDomainGeometry = 10,
    /** Formatting contexts: classification, establishment, participation. */
    RoboLayoutDomainFormattingContext = 11,
    /** Block layout: containing blocks, stacking, the geometry it writes. */
    RoboLayoutDomainBlockLayout = 12,
    /** Inline layout: lines, wrapping, the geometry it writes. */
    RoboLayoutDomainInlineLayout = 13,
    /** Text layout: ranges, measurement, the seam it goes through. */
    RoboLayoutDomainTextLayout = 14,
    /** Sizing: declared dimensions, availability, bounds, resolution. */
    RoboLayoutDomainSizing = 15,
    /** Margins: the four sides, the used values, the two policies. */
    RoboLayoutDomainMargins = 16,
    /** Padding: the four sides, the used values, the border and content boxes. */
    RoboLayoutDomainPadding = 17,
    /** Borders: the twelve longhands, the used widths, the two derived boxes. */
    RoboLayoutDomainBorder = 18,
    /** Positioning: the four insets, the precedence rule, the used displacement. */
    RoboLayoutDomainPosition = 19,
    /** Overflow: the two axes, the extents, the region and the clip rectangle. */
    RoboLayoutDomainOverflow = 20,
    /** Invalidation: the states, the reasons, the style classifier, the propagation. */
    RoboLayoutDomainInvalidation = 21
} LayoutDiagnosticDomain;

/** Number of defined diagnostic domains. */
#define RoboLayoutDomainCount 22u

/** Non-zero if @p domain is a defined diagnostic domain enumerator. */
int LayoutDiagnosticDomainIsKnown(LayoutDiagnosticDomain domain);

/** Human-readable domain name; never NULL; "unknown" when undefined. */
const char *LayoutDiagnosticDomainName(LayoutDiagnosticDomain domain);

/*
 * Bounds.
 *
 * A tree is heap-backed and grows, so there is no fixed object ceiling to state
 * here -- what a tree cannot exceed is the *document* it was built from, and
 * RoboHTML already bounds that (RoboHtmlMaxNodesPerDocument). What is
 * stated instead is the bound the builder needs: a DOM tree cannot be deeper
 * than its node count, so the recursion the builder uses is bounded by a bound
 * that already exists.
 */

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_TYPES_H */
