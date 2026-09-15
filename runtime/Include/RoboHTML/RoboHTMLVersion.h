/*
 * RoboHTML version pin — Phase 8.5 element-model milestone.
 *
 * 0.1.0 was the architecture-only surface (8.1); 0.2.0 added the tokenizer
 * (tokens, state machine, character references, raw-content families);
 * 0.3.0 added the tree builder (real document trees, insertion modes,
 * open-element stack, recovery, tree dump); 0.4.0 made the document the
 * authoritative model of that tree — metadata, explicit root ownership,
 * node ownership/cross-document checks, construction closure, reuse and
 * whole-tree invariant auditing. 0.5.0 makes the element a first-class part
 * of that model: namespace and classification, element state, the attribute
 * read boundary, and robo_html_element_create as the single construction
 * seam the tree builder and the future DOM both go through. The surface
 * still grows in 8.6+, so API freeze is NOT claimed here
 * (ROBO_HTML_API_FROZEN=0).
 */

#ifndef ROBO_HTML_VERSION_H
#define ROBO_HTML_VERSION_H

#define ROBO_HTML_VERSION_MAJOR 0
#define ROBO_HTML_VERSION_MINOR 5
#define ROBO_HTML_VERSION_PATCH 0
#define ROBO_HTML_VERSION_STRING "0.5.0"

/** 1 once the Phase 8 surface is frozen (not yet — see header comment). */
#define ROBO_HTML_API_FROZEN 0

#endif /* ROBO_HTML_VERSION_H */
