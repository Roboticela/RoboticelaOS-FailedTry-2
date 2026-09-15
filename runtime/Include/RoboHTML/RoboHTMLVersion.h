/*
 * RoboHTML version pin — Phase 8.3 tree-builder milestone.
 *
 * 0.1.0 was the architecture-only surface (8.1); 0.2.0 added the tokenizer
 * (tokens, state machine, character references, raw-content families);
 * 0.3.0 adds the tree builder (real document trees, insertion modes,
 * open-element stack, recovery, tree dump). The surface still grows in
 * 8.4+, so API freeze is NOT claimed here (ROBO_HTML_API_FROZEN=0).
 */

#ifndef ROBO_HTML_VERSION_H
#define ROBO_HTML_VERSION_H

#define ROBO_HTML_VERSION_MAJOR 0
#define ROBO_HTML_VERSION_MINOR 3
#define ROBO_HTML_VERSION_PATCH 0
#define ROBO_HTML_VERSION_STRING "0.3.0"

/** 1 once the Phase 8 surface is frozen (not yet — see header comment). */
#define ROBO_HTML_API_FROZEN 0

#endif /* ROBO_HTML_VERSION_H */
