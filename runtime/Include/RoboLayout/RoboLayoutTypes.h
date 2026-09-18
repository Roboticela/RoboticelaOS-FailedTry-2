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
 *     `robo_html_node_handle_t` -- an inert token RoboHTML defined -- so a caller
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
#define ROBO_LAYOUT_CLEARED { 0 }

/** Tree-scoped identity of one layout object. Never a pointer, never reused. */
typedef uint32_t robo_layout_node_id_t;

#define ROBO_LAYOUT_NODE_ID_NONE ((robo_layout_node_id_t)0u)

/**
 * What kind of box a layout object is.
 *
 * One value, and that is the point: 10.1 knows how to say "this element
 * generates a box" and nothing about what *kind* of box it is, because the
 * formatting contexts that would answer that are Phase 10.2.
 */
typedef enum robo_layout_object_kind {
    ROBO_LAYOUT_OBJECT_NONE = 0,
    /** An element that generates a box (any display except `none`). */
    ROBO_LAYOUT_OBJECT_BOX = 1
} robo_layout_object_kind_t;

/** Number of defined object kinds. */
#define ROBO_LAYOUT_OBJECT_KIND_COUNT 2u

/** Non-zero if @p kind is a defined object kind. */
int robo_layout_object_kind_is_known(robo_layout_object_kind_t kind);

/** Human-readable kind name; never NULL; "unknown" when undefined. */
const char *robo_layout_object_kind_name(robo_layout_object_kind_t kind);

/*
 * Diagnostic domains. The same discipline RoboHTML and RoboCSS use: a call
 * returns a plain robo_result_t and an optional caller-owned record refines the
 * failure with a domain and a message. There is no global mutable error state.
 */
typedef enum robo_layout_diagnostic_domain {
    ROBO_LAYOUT_DOMAIN_NONE = 0,
    /** Module lifecycle: not initialized, already initialized. */
    ROBO_LAYOUT_DOMAIN_MODULE = 1,
    /** Tree: creation, capacity, membership, lookup. */
    ROBO_LAYOUT_DOMAIN_TREE = 2,
    /** Objects: links, association and the read side. */
    ROBO_LAYOUT_DOMAIN_OBJECT = 3,
    /** Constraints: availability, bounds, clamping. */
    ROBO_LAYOUT_DOMAIN_CONSTRAINTS = 4,
    /** Results: the geometry a later goal fills in. */
    ROBO_LAYOUT_DOMAIN_RESULT = 5,
    /** Context: one layout pass and its counters. */
    ROBO_LAYOUT_DOMAIN_CONTEXT = 6,
    /** The DOM-to-tree builder. */
    ROBO_LAYOUT_DOMAIN_BUILDER = 7,
    /** The style source seam: what a consumer answers about one element. */
    ROBO_LAYOUT_DOMAIN_STYLE = 8,
    /** The DOM + computed-style bridge. */
    ROBO_LAYOUT_DOMAIN_DOM = 9,
    /** The geometry vocabulary (borrowed from RoboGraphics). */
    ROBO_LAYOUT_DOMAIN_GEOMETRY = 10
} robo_layout_diagnostic_domain_t;

/** Number of defined diagnostic domains. */
#define ROBO_LAYOUT_DOMAIN_COUNT 11u

/** Non-zero if @p domain is a defined diagnostic domain enumerator. */
int robo_layout_diagnostic_domain_is_known(robo_layout_diagnostic_domain_t domain);

/** Human-readable domain name; never NULL; "unknown" when undefined. */
const char *robo_layout_diagnostic_domain_name(robo_layout_diagnostic_domain_t domain);

/*
 * Bounds.
 *
 * A tree is heap-backed and grows, so there is no fixed object ceiling to state
 * here -- what a tree cannot exceed is the *document* it was built from, and
 * RoboHTML already bounds that (ROBO_HTML_MAX_NODES_PER_DOCUMENT). What is
 * stated instead is the bound the builder needs: a DOM tree cannot be deeper
 * than its node count, so the recursion the builder uses is bounded by a bound
 * that already exists.
 */

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_TYPES_H */
