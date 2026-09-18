/*
 * RoboCSS version pin — Phase 9.11 box-model milestone.
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
 * 0.8.0 adds the seventh, and it is the first that changes a value rather than
 * locating one: computed values (RoboCSSComputedValue.h). Inheritance says what
 * an element *has*; computation says what that is, and it is the difference
 * between the token `10px` and a length of ten pixels in the px unit, between
 * `#f00` and a colour, between `AUTO` and the keyword `auto`. The normaliser is
 * a function of the value's shape and the value (the property registry supplies
 * the shape), so it needs no context and no parent — which keeps the second half
 * of the milestone, `robo_css_style_context_compute_style`, a pure walk: resolve
 * a level, compute it, hand it to the level below as its parent. A child
 * therefore inherits its parent's *computed* value, not a recomputation of
 * something the parent specified. The container from 9.1 grew rather than
 * changed: an entry still holds the specified component value a 9.1 caller put
 * there and now also the name, the typed value, and a flag set saying whether it
 * was declared, inherited or defaulted. Three boundaries are crossed
 * deliberately and no further: `50%` stays a percentage and `2em` stays relative
 * because only layout can resolve either; `var()` and `calc()` are recognised,
 * carried intact with the *unresolved* flag, and never reduced, because a wrong
 * number is worse than a deferred one; and `currentColor` is carried the same
 * way rather than resolved against a default, since its value is the element's
 * own computed `color`. A value that means nothing for its property — `color:
 * nonsense` — is reported *invalid* rather than dropped, so a caller can see the
 * declaration was wrong instead of finding its property silently absent.
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
 *   computed      the typed value of one property: keyword, number, percentage,
 *                 length, colour, family list, preserved tokens, or invalid —
 *                 plus whether it needs a later stage to be resolved
 *   computed      (container) one element's computed style: per property the
 *                 specified value, the typed value and the provenance flags
 *   colour        one colour: a kind (resolved, contextual, invalid, none), the
 *                 four channels, every CSS spelling that produces one, the
 *                 substitution `currentColor` stands for, and one stable text
 *                 per kind
 *   unit          the closed unit vocabulary: each unit's spelling, family and
 *                 fixed factor to px, and which viewport a viewport unit
 *                 measures — rebuilt in 9.10 with a resolution context and a
 *                 resolver that can answer "not enough context"
 *   numeric       one typed number: a kind (number, integer, percentage,
 *                 length), its magnitude and — for a length — its unit, with
 *                 kind-aware equality and one canonical spelling
 *   box           the box model's vocabulary: the four sides in CSS's order, the
 *                 three shorthands and the longhand each side of each stands for,
 *                 and the 1/2/3/4 component mapping — with expansion and 
 *                 resolution kept deliberately apart
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
 * 0.9.0 adds the eighth, and it is the first that is a *representation* rather
 * than a stage: colours (RoboCSSColor.h). A colour is the one CSS value whose
 * meaning is arithmetic — three channels and an alpha, in a space, at a
 * precision — and 9.8's colour code lived inside the computed-value layer
 * because that was the only reader. 9.9 gives it a module, and the module owns
 * every spelling: the 148 standard named keywords, `transparent`, all four hex
 * lengths, `rgb()`/`rgba()` and `hsl()`/`hsla()` in both the legacy comma and the
 * modern space syntax, with numeric or percentage channels, hue as a number or in
 * `deg`/`grad`/`rad`/`turn`, and range clamping rather than wrapping. The value
 * layer now consumes that one parser instead of carrying its own, which is what
 * keeps two spellings of the same colour from disagreeing.
 *
 * The record is a *kind*, not just channels, and that is what makes
 * `currentColor` expressible: CURRENT is a colour that is not a colour yet, it is
 * typed and flagged through the value stage rather than approximated, and the
 * computed style resolves it at the one point that has the answer — against the
 * parent's computed `color` when the property *is* `color` (where `currentColor`
 * means `inherit`), and against the element's own computed `color` everywhere
 * else. With no parent computed style to read it stays contextual and says so.
 * Colours this build has no representation for (`hwb`, `lab`, `lch`, `oklab`,
 * `oklch`, `color()`, `color-mix`, `device-cmyk`) are carried with their tokens
 * and *reported* as deferred: the author wrote legal CSS, and calling it invalid
 * would be a lie while approximating it would be worse. Malformed colour calls
 * are reported as malformed, with the function named, which is a different
 * message from "not a colour" and a different message from "unknown colour
 * name" — three diagnostics, because they tell a developer three things.
 *
 * 0.10.0 adds the ninth, and it is the second *representation* rather than a
 * stage: units and numbers (RoboCSSUnit.h). A CSS value is mostly a number with
 * something attached, and this is the something — the closed unit vocabulary with
 * each unit's family and its fixed factor, exact conversion of the absolute units
 * to the reference pixel, and `robo_css_numeric_t`: a number, an integer, a
 * percentage or a dimension, owning nothing, with kind-aware and unit-aware
 * equality and one canonical spelling.
 *
 * The move is the same one 9.9 made and for the same reason: 9.8 kept the unit
 * table inside the computed-value layer because that was its only reader, and a
 * second reader — whatever has a font size, or a viewport — is what turns a table
 * a module's callers reach for into a module. The vocabulary grew with it: `lh`
 * and `rlh` beside `em` and `rem`, `vi`/`vb` for the two axes, and the small,
 * large and dynamic viewport families that `sv*`, `lv*` and `dv*` name.
 *
 * The decision the phase is built around is that resolving a length is a
 * *question* rather than a multiplication. `1in` is 96px by definition, so
 * `robo_css_unit_to_px` converts it and never consults a display; `2em` is not a
 * length until something says what the font size is, so
 * `robo_css_unit_resolve_px` takes a context and answers RESOLVED, NEEDS_CONTEXT,
 * NOT_A_LENGTH or INVALID. NEEDS_CONTEXT is the answer that matters: a unit this
 * build understands and a context that does not carry the fact is a value that
 * cannot be resolved *yet*, and inventing a font size or a viewport would produce
 * a number that looks computed and is wrong. The context is an argument and never
 * a global, with a `has_` flag on every fact so that "the font size is 16" and "I
 * do not know the font size" stay two different answers.
 *
 * A numeric value has four kinds and no fifth: NUMBER, INTEGER, PERCENTAGE and
 * LENGTH. They are separate because CSS's grammar separates them — a property
 * demanding an `<integer>` must refuse `1.5`, and `1.0` is a number token and must
 * be refused too, which is exactly the silent coercion a single "numeric" kind
 * would invite. Equality is kind-aware and unit-aware in the same spirit: `0` is
 * not `0%` and `0px` is not `0em`. A value that is none of the four is not a
 * numeric value, so it is a parse outcome rather than a kind — and the outcomes
 * stay apart because they tell a developer different things: an unknown unit is
 * `10furlongs`, a non-finite magnitude is `1e999`, a deferred value is `calc()`,
 * and a value that is not numeric at all is `red blue`.
 *
 * 0.11.0 adds the tenth, and it is the first that is a *property family* rather
 * than a representation: the box model (RoboCSSBox.h). A box is four edges, and
 * CSS writes them twice — four longhands, or one shorthand — so something has to
 * turn the second into the first. The vocabulary module owns that and nothing
 * else: the four sides in CSS's own order, the three shorthands this build
 * distributes (`margin`, `padding`, `border-width`), and the 1/2/3/4 component
 * mapping as a table with one row per arity rather than four branches per
 * shorthand.
 *
 * The decision the phase is built around is that *expansion is not resolution*.
 * Which declaration wins is the cascade's answer and stays the cascade's answer,
 * including when the two declarations carry different property names — `margin:
 * 10px; margin-left: 20px` is level, then specificity, then document order, asked
 * across two names instead of within one, which is why it is asked of the cascade
 * (`robo_css_cascade_candidate_compare`) and why nothing in the box model weighs
 * a level or compares a specificity. A distributed longhand is then resolved like
 * any other declaration, keywords and all: `margin: inherit` and `margin-left:
 * inherit` mean the same thing to all four edges because they go through the same
 * code, not because two implementations agree.
 *
 * Three shapes join the value layer, and they differ by *vocabulary* rather than
 * by syntax, which is the whole reason there are three: `auto` is a margin and is
 * not a padding, `medium` is a border width and is not a margin. Fourteen rows
 * join the registry — the twelve longhands, `border-width` as their third
 * shorthand and `box-sizing`, whose value model exists so a declaration survives
 * to the phase that has a layout rather than being dropped by the phase that does
 * not. None of them inherits, and none of them is painted: `border-width: 1px 2px`
 * becomes four values because everything downstream is written against the
 * longhands, not because this phase draws a border.
 *
 * A shorthand that cannot be distributed is *reported* rather than guessed at:
 * five components is an invalid declaration the grammar accepted, and
 * `margin: var(--edges)` is a legal declaration whose component count is not
 * known until substitution, which happens *before* expansion in CSS. Both leave
 * the author's text on the shorthand's own entry and count themselves as
 * unresolved, so a consumer can see that four edges are unanswerable instead of
 * reading a declaration and finding nothing underneath it.
 *
 * 0.12.0 is the eleventh and the first that is a *value* rather than a family or
 * a representation: `display` (RoboCSSDisplay.h). It is one property, and the
 * reason it is a shape rather than the generic KEYWORD every other identifier-
 * valued property uses is that `display` is the one thing every later phase reads
 * on every element. A keyword shape carries any identifier through, which is
 * right for `cursor: grab` — a value whose meaning belongs to a phase that has not
 * landed — and wrong here, because `display: bogus` would reach a layout pass as
 * a value it can only fail on. So the vocabulary is closed, and closed means an
 * unknown identifier is a mistake with a diagnostic rather than a keyword.
 *
 * The value carries three facts and not one, which is CSS Display 3's own model:
 * what box-level the element is (the *outer* display), what lays its contents out
 * (the *inner* display), and whether a box is generated at all. `display: none` is
 * therefore the absence of a box rather than zero geometry — the single confusion
 * the property invites, and the reason the classification is `generates_box`
 * asked of the table rather than a comparison against a keyword. `inline-block`
 * is not "inline, but flagged": it is an inline outer with a `flow-root` inner,
 * which is what CSS says when it defines it as `inline flow-root`, and the two
 * columns mean the next values are rows — `flex` an outer block with a flex
 * inner, `grid` likewise, `contents` a value that generates no box and is still
 * not `none`.
 *
 * Three boundaries are drawn rather than crossed, and each is a decision. HTML's
 * defaults (`div` block, `span` inline) are *not* here: they are a user-agent
 * stylesheet, and a vocabulary is not a stylesheet, so a `div` with no
 * declaration computes the property's initial value like anything else. The
 * multi-keyword syntax (`display: inline flex`) is refused, because it is a
 * grammar this build does not parse and a display value nothing can read is worse
 * than one CSS's own recovery rule drops. And no element is removed from any tree:
 * a computed `display: none` is a fact a later layout phase acts on, not an action
 * this one takes.
 *
 * Everything past that — layout, used values, `calc()` evaluation — is still a
 * declared boundary with no implementation behind it, and
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
#define ROBO_CSS_VERSION_MINOR 12
#define ROBO_CSS_VERSION_PATCH 0
#define ROBO_CSS_VERSION_STRING "0.12.0"

/** 1 once the Phase 9 surface is frozen (not yet — see header comment). */
#define ROBO_CSS_API_FROZEN 0

#endif /* ROBO_CSS_VERSION_H */
