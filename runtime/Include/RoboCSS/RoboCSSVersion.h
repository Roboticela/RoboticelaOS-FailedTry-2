/*
 * RoboCSS version pin — Phase 9.1 architecture milestone.
 *
 * 0.1.0 is the architecture-only surface. It contains, as real compilable
 * types with real constructors and deterministic lifetimes, the *data model*
 * the rest of Phase 9 is built on:
 *
 *   source        the bytes CSS arrives as, plus positions and ranges
 *   token         the CSS Syntax token record the tokenizer will fill
 *   value         component values: token, function, block, list
 *   property      identity for a property name, and the table that holds them
 *   declaration   name + value + importance + source range + state
 *   selector      simple / compound / complex selectors and a selector list
 *   rule          style rules and at-rules, with their declaration and
 *                 nested-rule storage
 *   stylesheet    origin, state, source, rule storage and traversal
 *   error         the CSS error vocabulary, recoverable distinguished from fatal
 *   parser        a cursor over a source with position tracking and the
 *                 first-error record, and no tokenizer behind it
 *
 * Everything that would *interpret* that model — tokenizing, parsing,
 * matching, cascade, specificity, inheritance, computed values, colours,
 * units, the box model — is a declared boundary in this phase and answers
 * ROBO_NOT_SUPPORTED. No stub pretends to parse or to match, and
 * robo_css_get_capabilities reports exactly what is and is not here so a
 * caller never has to guess.
 *
 * The surface will keep growing for the whole of Phase 9, so API freeze is
 * NOT claimed here (ROBO_CSS_API_FROZEN=0).
 */

#ifndef ROBO_CSS_VERSION_H
#define ROBO_CSS_VERSION_H

#define ROBO_CSS_VERSION_MAJOR 0
#define ROBO_CSS_VERSION_MINOR 1
#define ROBO_CSS_VERSION_PATCH 0
#define ROBO_CSS_VERSION_STRING "0.1.0"

/** 1 once the Phase 9 surface is frozen (not yet — see header comment). */
#define ROBO_CSS_API_FROZEN 0

#endif /* ROBO_CSS_VERSION_H */
