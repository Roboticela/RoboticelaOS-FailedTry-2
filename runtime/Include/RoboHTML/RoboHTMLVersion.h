/*
 * RoboHTML version pin — Phase 8.2 tokenizer milestone.
 *
 * 0.1.0 was the architecture-only surface (8.1). 0.2.0 adds the tokenizer
 * (tokens, state machine, character references, raw-content families).
 * The surface still grows in 8.3 (tree builder), so API freeze is NOT
 * claimed here (ROBO_HTML_API_FROZEN=0) until those phases land.
 */

#ifndef ROBO_HTML_VERSION_H
#define ROBO_HTML_VERSION_H

#define ROBO_HTML_VERSION_MAJOR 0
#define ROBO_HTML_VERSION_MINOR 2
#define ROBO_HTML_VERSION_PATCH 0
#define ROBO_HTML_VERSION_STRING "0.2.0"

/** 1 once the Phase 8 surface is frozen (not yet — see header comment). */
#define ROBO_HTML_API_FROZEN 0

#endif /* ROBO_HTML_VERSION_H */
