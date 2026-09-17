/*
 * RoboCSS version pin — Phase 9.2 tokenizer milestone.
 *
 * 0.1.0 was the architecture-only surface: the data model the rest of Phase 9
 * is built on, with every interpretation site answering ROBO_NOT_SUPPORTED.
 *
 * 0.2.0 keeps all of that and adds the first real machine on top of it — the
 * CSS Syntax tokenizer, which turns source bytes into that model's tokens:
 *
 *   source        the bytes CSS arrives as, plus positions and ranges
 *   token         the CSS Syntax token record, and now the token's *value*:
 *                 escapes resolved and delimiters stripped, computed from the
 *                 token's own text with no buffer and no source pointer
 *   tokenizer     the state machine: idents, functions, at-keywords, hashes,
 *                 strings and bad strings, urls and bad urls, numbers,
 *                 percentages, dimensions, whitespace, comments, CDO/CDC,
 *                 punctuation, EOF, and an explicit INVALID for bytes that
 *                 cannot begin a token at all
 *   value         component values: token, function, block, list
 *   property      identity for a property name, and the table that holds them
 *   declaration   name + value + importance + source range + state
 *   selector      simple / compound / complex selectors and a selector list
 *   rule          style rules and at-rules, with their declaration and
 *                 nested-rule storage
 *   stylesheet    origin, state, source, rule storage and traversal
 *   error         the CSS error vocabulary, recoverable distinguished from fatal
 *   parser        a cursor over a source with position tracking and the
 *                 first-error record, and no grammar behind it
 *
 * Everything past lexing — the grammar, selector matching, the cascade,
 * specificity, inheritance, computed values, colours, units, the box model —
 * is still a declared boundary that answers ROBO_NOT_SUPPORTED. No stub
 * pretends to parse or to match, and robo_css_get_capabilities reports exactly
 * what is and is not here, so a caller never has to guess.
 *
 * The surface will keep growing for the whole of Phase 9, so API freeze is
 * NOT claimed here (ROBO_CSS_API_FROZEN=0).
 */

#ifndef ROBO_CSS_VERSION_H
#define ROBO_CSS_VERSION_H

#define ROBO_CSS_VERSION_MAJOR 0
#define ROBO_CSS_VERSION_MINOR 2
#define ROBO_CSS_VERSION_PATCH 0
#define ROBO_CSS_VERSION_STRING "0.2.0"

/** 1 once the Phase 9 surface is frozen (not yet — see header comment). */
#define ROBO_CSS_API_FROZEN 0

#endif /* ROBO_CSS_VERSION_H */
