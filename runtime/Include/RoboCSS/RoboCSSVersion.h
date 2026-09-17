/*
 * RoboCSS version pin — Phase 9.3 parser milestone.
 *
 * 0.1.0 was the architecture-only surface: the data model the rest of Phase 9
 * is built on, with every interpretation site answering ROBO_NOT_SUPPORTED.
 *
 * 0.2.0 kept all of that and added the first real machine on top of it — the
 * CSS Syntax tokenizer, which turns source bytes into that model's tokens.
 *
 * 0.3.0 adds the second: the grammar that consumes the tokenizer and builds the
 * model out of real CSS.
 *
 *   source        the bytes CSS arrives as, plus positions and ranges
 *   token         the CSS Syntax token record, and the token's *value*:
 *                 escapes resolved and delimiters stripped, computed from the
 *                 token's own text with no buffer and no source pointer
 *   tokenizer     the state machine: idents, functions, at-keywords, hashes,
 *                 strings and bad strings, urls and bad urls, numbers,
 *                 percentages, dimensions, whitespace, comments, CDO/CDC,
 *                 punctuation, EOF, and an explicit INVALID for bytes that
 *                 cannot begin a token at all
 *   value         component values: token, function, block, list — and now the
 *                 tree a prelude or a declaration's value actually is
 *   property      identity for a property name, and the table that holds them
 *   declaration   name + value + importance + source range + state
 *   selector      simple / compound / complex selectors and a selector list
 *   rule          style rules and at-rules, with their declaration and
 *                 nested-rule storage, their prelude as component values, and
 *                 whether their block was ever closed
 *   stylesheet    origin, state, source, rule storage, traversal, and now a
 *                 real parse: robo_css_stylesheet_parse tokenizes the attached
 *                 source, runs the grammar, and replaces the rules
 *   error         the CSS error vocabulary, recoverable distinguished from fatal
 *   parser        a cursor over a source with position tracking and the
 *                 first-error record, plus the grammar behind it
 *
 * What the grammar is: the CSS Syntax structure rules — a stylesheet, a list of
 * rules, an at-rule, a qualified rule, a block's declarations, a declaration,
 * and component values — with the specification's bounded recovery (skip to the
 * `;`, the enclosing `}`, or a balanced block) and a nesting budget so that
 * adversarial input costs bounded stack rather than a crash. At-rules are
 * syntax only: no `@media` is evaluated, no `@import` is fetched, and no
 * at-rule name is looked up in a table, because there is no table. The inline
 * `style` boundary (9.1.10) is filled by the same declaration-list grammar.
 *
 * Everything past parsing — selector matching, the cascade, specificity,
 * inheritance, computed values, colours, units, the box model — is still a
 * declared boundary with no implementation behind it. A style rule's prelude is
 * component values and `selectors` is empty until 9.4 fills it. No stub pretends
 * to parse or to match, and robo_css_get_capabilities reports exactly what is
 * and is not here, so a caller never has to guess.
 *
 * The surface will keep growing for the whole of Phase 9, so API freeze is
 * NOT claimed here (ROBO_CSS_API_FROZEN=0).
 */

#ifndef ROBO_CSS_VERSION_H
#define ROBO_CSS_VERSION_H

#define ROBO_CSS_VERSION_MAJOR 0
#define ROBO_CSS_VERSION_MINOR 3
#define ROBO_CSS_VERSION_PATCH 0
#define ROBO_CSS_VERSION_STRING "0.3.0"

/** 1 once the Phase 9 surface is frozen (not yet — see header comment). */
#define ROBO_CSS_API_FROZEN 0

#endif /* ROBO_CSS_VERSION_H */
