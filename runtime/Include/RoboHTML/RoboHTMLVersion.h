/*
 * RoboHTML version pin — Phase 8.9 DOM mutation milestone.
 *
 * 0.1.0 was the architecture-only surface (8.1); 0.2.0 added the tokenizer
 * (tokens, state machine, character references, raw-content families);
 * 0.3.0 added the tree builder (real document trees, insertion modes,
 * open-element stack, recovery, tree dump); 0.4.0 made the document the
 * authoritative model of that tree — metadata, explicit root ownership,
 * node ownership/cross-document checks, construction closure, reuse and
 * whole-tree invariant auditing; 0.5.0 made the element a first-class part
 * of that model — namespace and classification, element state, and
 * robo_html_element_create as the single construction seam the tree builder
 * and the future DOM both go through. 0.6.0 replaces the temporary
 * attribute table with the authoritative attribute model: one
 * robo_html_attribute_t used by the tokenizer's staging, the stored payload
 * and the readers alike; deterministic order; presence distinct from an
 * empty value; and add/set/remove on a live element. 0.7.0 makes character
 * data a modelled node: real Text nodes created through one seam
 * (robo_html_text_create), content access over the same payload arena every
 * other node kind uses, set/append mutation, byte-exact whitespace and
 * Unicode, and text runs that coalesce per run rather than per token. 0.8.0
 * makes the tree navigable: one authoritative traversal surface for every
 * node kind — relationships in both directions, absolute depth, a
 * deterministic pre-order enumeration, an O(n) child cursor, the
 * all/elements/text filters, and bounded search — replacing three separate
 * copies of the pre-order successor with the one implementation all of them
 * now share. 0.9.0 makes that tree editable: append, insert before, remove,
 * replace and bulk child removal, all through the same linking primitive the
 * parser uses, with one-parent and acyclic invariants enforced, cross-document
 * mutation refused rather than guessed, detachment as a legal state that the
 * whole-tree audit now reports separately from corruption, and a monotonic
 * tree version that lets traversal report an iteration invalidated by a
 * structural change instead of returning a node that is no longer where it
 * was read from. The surface still grows in 8.10+, so API freeze is NOT
 * claimed here (ROBO_HTML_API_FROZEN=0).
 */

#ifndef ROBO_HTML_VERSION_H
#define ROBO_HTML_VERSION_H

#define ROBO_HTML_VERSION_MAJOR 0
#define ROBO_HTML_VERSION_MINOR 9
#define ROBO_HTML_VERSION_PATCH 0
#define ROBO_HTML_VERSION_STRING "0.9.0"

/** 1 once the Phase 8 surface is frozen (not yet — see header comment). */
#define ROBO_HTML_API_FROZEN 0

#endif /* ROBO_HTML_VERSION_H */
