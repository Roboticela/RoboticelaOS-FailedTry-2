/*
 * RoboHTML version pin — Phase 8.1 architecture milestone.
 *
 * The Phase 8.1 surface is architecture-only: it is expected to grow in
 * 8.2 (tokenizer) and 8.3 (tree builder), so API freeze is NOT claimed
 * here (ROBO_HTML_API_FROZEN=0) until those phases land.
 */

#ifndef ROBO_HTML_VERSION_H
#define ROBO_HTML_VERSION_H

#define ROBO_HTML_VERSION_MAJOR 0
#define ROBO_HTML_VERSION_MINOR 1
#define ROBO_HTML_VERSION_PATCH 0
#define ROBO_HTML_VERSION_STRING "0.1.0"

/** 1 once the Phase 8 surface is frozen (not yet — see header comment). */
#define ROBO_HTML_API_FROZEN 0

#endif /* ROBO_HTML_VERSION_H */
