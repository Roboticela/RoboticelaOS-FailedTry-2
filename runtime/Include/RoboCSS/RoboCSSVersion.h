/*
 * RoboCSS version pin — Phase 9.7 inheritance milestone.
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
 * 0.4.0 adds the third: the selector grammar, which turns a rule's prelude into
 * real selectors — names resolved through the tokenizer's own escape rule,
 * `:not()` arguments parsed into owned nested lists, An+B parsed once — and the
 * matcher, which answers "does this element satisfy this selector" by asking a
 * *host* (RoboCSSStyle.h) for one element at a time. No DOM is copied, no
 * document is walked, and neither RoboHTML nor any other subsystem is included.
 *
 * 0.5.0 adds the fourth, and it is the first thing in the subsystem that
 * *applies* anything: the cascade. robo_css_style_context_match_declarations is
 * now real — it walks the context's sheets by origin, matches their rules
 * through the same host, parses the element's `style` attribute through the same
 * grammar a rule's block uses, and hands back a robo_css_cascade_t
 * (RoboCSSCascade.h) in cascade order. The level table (origin × importance,
 * with inline placed where a browser places it and the important half mirrored
 * the way CSS mirrors it) is complete and final. Everything the collection could
 * not use — an undecided selector, an at-rule block nothing evaluates, a sheet
 * destroyed while referenced, a rule whose prelude was never parsed as selectors
 * — is counted on the result rather than dropped.
 *
 * 0.6.0 added the fifth and completed the ordering: specificity
 * (RoboCSSSpecificity.h). A selector's specificity is three counts — ids, then
 * classes/attributes/pseudo-classes, then types/pseudo-elements — compared as a
 * tuple and saturated rather than wrapped, computed from the same selector
 * records matching reads. Every level of the 9.1 model has a door, `:not()`
 * takes its argument's most specific selector rather than being a class selector
 * like any other, and a rule's selector list is worth the specificity of the
 * member that *matched* the element. The cascade now consumes all three keys and
 * holds no specificity arithmetic of its own, so 9.5's
 * ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY is gone: there is nothing left
 * for it to describe.
 *
 * 0.7.0 adds the sixth, and it is the first that answers a question the cascade
 * cannot: inheritance (RoboCSSInherit.h). A cascade says which declaration wins
 * for an element; a resolved style says what the element *has*, which for the
 * properties it says nothing about means the parent's answer (an inherited
 * property) or the property's own initial value. The primitive door takes the
 * parent's resolved style as an argument, so a resolution is a pure function of
 * its inputs and nothing is cached; the context door walks to the root and
 * resolves back down, so "the immediate resolved parent" is literal. The
 * CSS-wide keywords are obeyed here — `inherit`, `initial`, `unset` — and
 * `revert` is recognised, carried unresolved and *reported*, because obeying it
 * means re-running a cascade and half-obeying it would be wrong in exactly the
 * cases it exists for. A resolved value is not a computed one: `10px` is still
 * `10px` and `red` is still `red`, and `computed_values` stays 0. The property
 * registry (RoboCSSPropertyRegistry.h) is the other half of the milestone: CSS's
 * own facts about a property — does it inherit by default, and what is its
 * initial value — held apart from a caller's property *table*, which is identity.
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
 *   definitions   CSS's own facts about a property: does it inherit by default,
 *                 and what is its initial value (RoboCSSPropertyRegistry.h)
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
 *   inherited     what one element resolved to: one entry per property it has an
 *                 answer for, each with where the value came from
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
 * Everything past that — computed values, colours, units, the box model — is
 * still a declared boundary with no implementation behind it, and
 * `robo_css_capabilities_t` reports exactly what is and is not here so a caller
 * never has to guess. Three boundaries are deliberately not crossed rather than
 * merely unfinished, and each is a decision: `:hover` and its state-dependent
 * relatives are *reported* as unevaluatable rather than assumed; a pseudo-element
 * matches no element because generated content does not exist yet; and `revert`
 * is recognised, carried and reported rather than obeyed, because obeying it
 * means re-running a cascade with one origin removed and a half-answer would be
 * wrong in exactly the cases `revert` exists for. The cascade, by contrast, is
 * now total: with specificity in hand there is no ordering question it cannot
 * answer, which is why 9.5's reported-undecided verdict no longer exists — and
 * inheritance is total in the same sense, because every property an element
 * does not declare has an answer: the parent's, or its own initial value.
 *
 * The surface will keep growing for the whole of Phase 9, so API freeze is
 * NOT claimed here (ROBO_CSS_API_FROZEN=0).
 */

#ifndef ROBO_CSS_VERSION_H
#define ROBO_CSS_VERSION_H

#define ROBO_CSS_VERSION_MAJOR 0
#define ROBO_CSS_VERSION_MINOR 7
#define ROBO_CSS_VERSION_PATCH 0
#define ROBO_CSS_VERSION_STRING "0.7.0"

/** 1 once the Phase 9 surface is frozen (not yet — see header comment). */
#define ROBO_CSS_API_FROZEN 0

#endif /* ROBO_CSS_VERSION_H */
