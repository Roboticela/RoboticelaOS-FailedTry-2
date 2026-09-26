/*
 * RoboCSS version pin — Phase 9.16 conformance milestone.
 *
 * 0.1.0 was the architecture-only surface: the data model the rest of Phase 9
 * is built on, with every interpretation site answering RoboNotSupported.
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
 * *applies* anything: the cascade. CssStyleContextMatchDeclarations is
 * now real — it walks the context's sheets by origin, matches their rules
 * through the same host, parses the element's `style` attribute through the same
 * grammar a rule's block uses, and hands back a CssCascade
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
 * `10px` and `red` is still `red`, and `computedValues` stays 0. The property
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
 * of the milestone, `CssStyleContextComputeStyle`, a pure walk: resolve
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
 *                 real parse: CssStylesheetParse tokenizes the attached
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
 *   position      the positioning vocabulary: the five values, each with whether
 *                 its box stays in flow and whether it is a positioned box, and
 *                 the classifications a layout pass dispatches on — with the four
 *                 inset properties named by the side vocabulary and shaped by the
 *                 box model's EDGE_OFFSET
 *   background    the background vocabulary: `background-repeat` (four values,
 *                 each a row carrying its two axes) and `background-attachment`
 *                 (two), the edge vocabulary a position keyword is spelled by, the
 *                 position and size *pairs*, and the classifier that distributes
 *                 one `background:` layer over its six longhands
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
 * to the reference pixel, and `CssNumeric`: a number, an integer, a
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
 * `CssUnitToPx` converts it and never consults a display; `2em` is not a
 * length until something says what the font size is, so
 * `CssUnitResolvePx` takes a context and answers RESOLVED, NEEDS_CONTEXT,
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
 * (`CssCascadeCandidateCompare`) and why nothing in the box model weighs
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
 * the property invites, and the reason the classification is `generatesBox`
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
 * 0.13.0 is the twelfth, and it adds the second closed vocabulary plus the first
 * shape whose value is a *number* rather than an enumerator or a length:
 * positioning (RoboCSSPosition.h).
 *
 * `position` gets a vocabulary of its own for the reason `display` did, and the
 * shape of that reasoning is worth stating once because it now holds twice: a
 * property every later phase *dispatches on* cannot be the generic KEYWORD, which
 * carries any identifier through and leaves the discovery of a mistake to
 * whichever phase first reads the value. `position: banana` is therefore a
 * mistake with a diagnostic, and `position: sticky` is a row — a value this build
 * represents and classifies but cannot *behave* on, because what a sticky box does
 * relative to a scrollport is a scrolling question and refusing the value outright
 * would drop the legal declaration and its offsets with it.
 *
 * Each row carries two facts rather than one, and they are asked by different
 * phases: whether the box stays **in flow** (`static`, `relative`, `sticky`), and
 * whether it is **positioned** (everything but `static`). The second is the one
 * that does two jobs — it is what makes an offset apply at all, `top` on a `static`
 * element being ignored rather than honoured, and it is what makes a box a
 * containing block for an absolutely positioned descendant. `relative` and
 * `sticky` answer yes to both, which reads as a contradiction until you notice
 * they are two questions.
 *
 * The four inset properties are rows and not a vocabulary, and that is a decision
 * rather than an omission: `top`, `right`, `bottom` and `left` take
 * `<length-percentage> | auto`, which is exactly the box model's EDGE_OFFSET
 * grammar, so they reuse that shape instead of getting a second one with the same
 * vocabulary — which is how `top: 20%` and `margin-top: 20%` would come to be
 * normalised differently. Their names are the side vocabulary's, in CSS's own
 * order. `z-index` joins them with the phase's third shape, `auto` or a whole
 * number, and the whole-number question is asked of the unit module rather than
 * answered in the value layer: `<integer>` is a grammar, so `1.0` is a number token
 * and is refused beside `1.5`, each with its own diagnostic because each is a
 * different near miss.
 *
 * Six non-inherited registry rows, and no geometry. Nothing here computes a
 * containing block, an offset, a stacking order or a scroll position; a percentage
 * inset stays a percentage, an `auto` inset stays `auto` — which is not a
 * placeholder, since turning it into `0` would place every absolutely positioned
 * box against the top-left corner of its containing block — and `z-index: auto`
 * stays a keyword because it says something `0` does not.
 *
 * 0.14.0 is the thirteenth, and it adds the first property *family* whose parts
 * are not the same kind of thing: backgrounds (RoboCSSBackground.h). The box
 * model's four edges are four of one value; `background` is one declaration that
 * speaks for six properties of six different shapes — a colour, an image
 * reference, a repeat style, a pair of positions, a pair of sizes and an
 * attachment — and that is the whole reason it is a module rather than another
 * row in the box's. There is no positional mapping from component to longhand,
 * because a component's *kind* is what decides where it goes, and the classifier
 * is where that is written down.
 *
 * The two closed vocabularies join the shared grammar rather than growing their
 * own: `background-repeat` (`repeat`, `repeat-x`, `repeat-y`, `no-repeat`) and
 * `background-attachment` (`scroll`, `fixed`) are rows in the table `display` and
 * `position` already use, so a third and fourth closed set are two more rows and
 * none of the refusal code is written twice. `repeat-x` is not a fifth behaviour
 * — it is "tiles horizontally, does not tile vertically" — so the row carries
 * both *axes* and `repeats_on_axis` is derived from it, exactly as the display
 * row carries an outer and an inner display. A value added later (`space`,
 * `round`) answers for itself.
 *
 * The position pair is the first shape whose value is *two* components, and the
 * pair is the value because CSS's single-component forms are folded into it:
 * `center` computes to `center center`. Its components are per-*axis* rather than
 * one shape with two optional halves, because a position keyword is spelled by
 * its axis — `left` is the START edge horizontally and `top` is the START edge
 * vertically — and `left 10px` is the edge-relative form, a different grammar
 * whose *order* carries the meaning, refused rather than half-read. `background-size`
 * is the same pair shape with three whole-value keywords beside it, and a pair of
 * two `auto`s folds back to `auto` because they are one computed value with one
 * spelling.
 *
 * Six longhands, none of which inherits — a background is painted per element,
 * and a child that inherited one would tile its parent's image over its own box —
 * and the shorthand is a *pass* rather than a table row, because its third
 * outcome differs from the box model's in the way that matters: a box edge the
 * author did not write has no value, while a background longhand the author did
 * not write is **reset to its initial value**, since the shorthand stands for all
 * six. That is what makes `background: red` mean "just this" on an element an
 * earlier rule gave an image, and it is asserted from both sides.
 *
 * Nothing paints: no image is loaded, no percentage is resolved against a box, and
 * multiple background *layers* (`url(a.png), url(b.png)`) are refused with a
 * reason because the shorthand grammar for them is a list of layers and each
 * layer is a whole set of longhands. What the phase converts is the one thing
 * that is exact — an absolute length arrives as the reference pixel, so
 * `background-position: 1in` and `margin-top: 1in` cannot come to disagree about
 * what a computed length is.
 *
 * 0.15.0 is the fourteenth, and it is the first phase whose subject is not one
 * property or one family but a *grid*: borders (RoboCSSBorder.h) are three kinds
 * over four sides, twelve longhands, and four different declarations can set any
 * one of them — the longhand, its positional row (`border-width`), its side
 * shorthand (`border-top`) and `border` itself. That is why this phase's central
 * act is a *move*: the distribution of the three border rows left the box model's
 * pass and joined a pass that can see all four declarers at once, because a
 * longhand two passes materialise is a longhand with two entries. The box table
 * kept the rows, and that half had to stay — the twelve names are a vocabulary one
 * reader should be able to walk, and `border-top-width` appears in the build once.
 *
 * The style vocabulary is the fifth closed set and a row in the same table the
 * other four use, so a sixth is a row. What is new is the *classifier*: a
 * composite shorthand's components map to a kind rather than to a position or a
 * longhand, and the order the questions are asked in is the whole logic — a style
 * keyword before a width keyword before a colour *name*, because `none` and
 * `hidden` are styles, `thin`/`medium`/`thick` are the only identifiers left that
 * are certainly not colours, and `red` can be settled by nothing else. A `var()`
 * is a fourth answer rather than a guess, for the reason it always is.
 *
 * Two behaviours differ from the two families beside it, and each is CSS's own.
 * A kind a composite shorthand did not name is **reset to its initial value**,
 * as a background longhand is — `border: 1px solid` really does say the border
 * has no colour of its own. And a *positional* row has no such case at all: one to
 * four components always cover all four sides, so nothing is ever left unnamed in
 * a `border-width`. The colour initial is `currentcolor`, which is the one initial
 * value in the registry that is a *reference*, and it is why the four colour
 * longhands cannot be `transparent` the way `background-color` is: a border whose
 * colour had been resolved at registration time would be a border nobody could
 * see and no author could explain. Nothing paints, and no edge is measured —
 * `border-radius` is not a value this build has.
 *
 * Everything past that — layout, used values, `calc()` evaluation — is still a
 * declared boundary with no implementation behind it, and
 * `CssCapabilities` reports exactly what is and is not here so a caller
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
 * 0.16.0 is the fifteenth, and it is the only one that adds no surface at all:
 * it is the *conformance* phase (`css/tests/SuiteRoboCssConformance.c`), and the
 * version moves because the build's *promises* did rather than because a header
 * did. Phase 9's forty per-family suites each know one family, and none of them
 * can ask the questions that are only true of the product: that every one of the
 * 63 registered properties' initial values computes through the normaliser the
 * registry declares (a row whose shape and whose initial had drifted apart
 * computes INVALID on every element of every document, and
 * `css.registry.initial_values` asserts the initial *shape*, which is a 9.1 fact
 * and would not notice); that one representative declaration per property
 * produces one exact computed value *and kind*; that one realistic document's
 * every family agrees on the same element; and that malformed CSS, identical
 * bytes and a repeated pipeline all behave. Two of its assertions are findings
 * rather than summaries: `caps.backgrounds` and `caps.borders` were each
 * documented by their phase as having flipped to 1 and were **asserted nowhere in
 * the build** — `css.capabilities` stops at `positioning` and then tests the two
 * flags that are 0 — so the capability struct is now walked field by field. The
 * other is that the determinism test compares whole *styles* by
 * `CssComputedValueEquals` rather than by text, so a difference in
 * formatting cannot hide one in value or the reverse.
 *
 * 0.17.0 is the sixteenth, and it is the first version in this module added by a
 * *layout* phase rather than by one of the 9.x value phases. Phase 10.13 found that
 * `overflow` — the one property its whole phase is about — had no row here at all,
 * so the layout side could not consume a computed value because there was none. The
 * fix is the smallest one that keeps the module's own discipline: three registry
 * rows (`overflow`, `overflow-x`, `overflow-y`), a closed two-value vocabulary with
 * the one classification a consumer dispatches on (`clips`), and a computed-value
 * kind — which is 9.12's, 9.13's and 9.15's move for a fourth property, made by the
 * phase that reads it.
 *
 * Two facts about it are worth reading here rather than being discovered. The
 * **shorthand's own name takes the vocabulary**, because nothing in this build
 * expands a shorthand: `overflow` left on the generic PRESERVE shape would carry
 * tokens no consumer reads, and `overflow: hidden` — a declaration authors write on
 * nearly every box that looks scrollable — would lay out exactly as if it were
 * absent. One vocabulary shared by the shorthand and its two longhands is the
 * shorthand's *meaning* one step earlier than CSS computes it, and the cost is
 * stated where it is paid: the two-value form (`overflow: hidden visible`) is not
 * read. And `scroll`, `auto` and `clip` are **rows this build has not added** rather
 * than words it does not know, for the reason `flex` and `grid` are absent from
 * `display`: each is a behaviour — scrolling, a scrollport, a second clipping rule —
 * that no phase in this build has, and reading one as `visible` would be a silent
 * wrong answer about geometry.
 *
 * 0.18.0 is the seventeenth, and it is the **second** version in this module added by a
 * phase that does not belong to Phase 9. Phase 11.9 needed `opacity` -- the one property a
 * painter reads that changes neither a box's geometry nor a colour, but the strength at
 * which the box and everything inside it reaches a surface -- and it found no row here, so
 * the smallest fix that keeps the module's discipline is one registry row, one computation
 * shape and one computed-value kind.
 *
 * Three facts about it are worth the space, because two of them are ways this property is
 * unlike every other one in the table.
 *
 * The shape is the first here that is neither a closed vocabulary nor a length family. Its
 * set is every real in `[0, 1]`, so its questions are arithmetic rather than a table
 * lookup, and it is the first property in this build whose value is bounded on **both**
 * sides -- which is why the range rule lives in one place (`RoboCSSOpacity.h`) instead of
 * in every consumer. An out-of-range declaration is **clamped** rather than refused, which
 * is CSS's own rule and is a deliberate exception to this module's habit of making a wrong
 * value a diagnostic: `opacity: 2` is legal CSS meaning `1`, and refusing it would make a
 * document that every browser renders correctly render here as if the declaration had
 * never been written.
 *
 * Its initial value is `1`, and that is the row to read twice. Every other property's
 * initial value is the zero of its own enumerator -- `visible`, `static`, `inline`, an
 * absent colour -- so a zeroed record holds the right answer by accident of the enumerator
 * order. Opacity's initial value is **one**, so the accident would hold "fully transparent"
 * and every element of every document that never mentions the property would be invisible.
 * The record therefore writes its initial value explicitly (`CssComputedValueInit`)
 * and the accessor answers it for any other kind, which closes the trap from both sides.
 *
 * And it is classified non-inherited, which is not the same fact as "a child is drawn at
 * its parent's strength" and is deliberately the opposite of it. Inheritance copies a value
 * *down*; opacity *multiplies along a path*, which is an accumulation and belongs to the
 * layer that walks the path. Registering it as inherited would make a child at `opacity: 1`
 * inside a parent at `0.5` fully opaque again, where CSS renders it at `0.5` -- the mistake
 * that produces the wrong picture rather than a missing one.
 *
 * 0.19.0 is the eighteenth, and it is the first version added by **Phase 18**
 * rather than by a 9.x or layout/paint phase: it is the second half of CSS
 * Variables, and the phase is the CSS architecture's own completion. 9.x made a
 * custom property a first-class declaration — stored as an opaque token
 * sequence, ordered by the cascade and copied down by inheritance — and then
 * stopped exactly where the specification splits: a `var()` reference was
 * recognised, carried intact and flagged unresolved, and nothing ever
 * substituted it. This version substitutes it, in one new module
 * (`RoboCSSVariable.h`) run at computed-value time.
 *
 * Three properties of the pass are what make it this module's kind of code. It
 * is a *pure function* of (the value, a computed style carrying the custom
 * properties in scope) and reaches no DOM, no font and no box, because a
 * reference resolves before the property's own shape looks at the value — which
 * is why one pass serves `color: var(--brand)` and `width: var(--w)` alike. Its
 * result is a *token sequence and not a token*: `--m: 10px 20px` makes a
 * reference to it a two-component value, so collapsing it here would lose the
 * shape the normaliser needs. And a reference that cannot be substituted —
 * undefined with no fallback, or a cycle — is answered as CSS's *invalid at
 * computed-value time*: the declaration does not take a value, and the property
 * follows `unset`, which is the parent's computed value for an inherited
 * property and its own initial value otherwise. A build that returned a failure
 * code or an empty value instead would be choosing between dropping the
 * declaration and inventing one for it, and neither is true.
 *
 * Cycles are bounded by a *depth* rather than made an error. A name already on
 * the resolution stack answers not-found, so a cyclic pair takes the fallback if
 * it wrote one and is `unset` otherwise, and the bound
 * (`RoboCssVariableMaxDepth`) exists for the deep-but-acyclic chain, which must
 * not be answered by a stack overflow. Nothing here caches: a custom property is
 * read through the same computed-style lookup every other consumer uses, so a
 * mutation cannot leave a substituted value behind.
 *
 * Two capabilities join the table — `customProperties` and
 * `variableSubstitution` — and one boundary is stated rather than crossed: the
 * inheritance pass expands a shorthand *before* this pass runs, so a shorthand
 * whose component count a substitution would change (`margin: var(--m)`) keeps
 * its own entry and takes `unset` rather than being distributed into four edges.
 * Lifting that is a pass-order change in the phase that owns expansion, and it
 * is named in `Architecture.md` rather than implied.
 *
 * 0.20.0 is the nineteenth, and it is the second version added by Phase 18: it
 * is the **object model**, the half of CSS this module had never had. Every
 * phase before it turned bytes into structure and *read* that structure; not
 * one of them turned it back into text, and not one of them changed it. This
 * version does both, in one new module (`RoboCSSOM.h`).
 *
 * Its first half is `cssText`: a canonical rendering of a component value, a
 * selector list, a declaration, a rule, a rule list or a whole stylesheet. The
 * rendering is deliberately not the specification's byte-exact serialization —
 * no number canonicalisation, no colour normalisation — and it is deliberately
 * *re-parseable*, because the property worth having is that the model survives
 * a round trip through text, and a selector name that could be misread is
 * escaped when it is written (`foo:bar` comes back as `.\3a bar`).
 *
 * Its second half is `CssStyleDeclaration`, CSSOM's `element.style`: a
 * self-contained, mutable set of properties that can be parsed, read, edited,
 * removed and rendered. It is a *second* declaration container rather than a
 * mutable view of the first because a parsed declaration borrows from its
 * source — that is what makes parsing cheap — while `setProperty` has to work
 * after the source is gone, so each property copies its name's bytes, owns its
 * value's bytes and re-parses the value over them. A property set twice keeps
 * its first position and takes the new value; an empty value is a removal; a
 * name compares ASCII case-insensitively and is stored as written.
 *
 * One more thing is true here and was not before: a rule can be *edited*. The
 * sheet is a live object whose top-level rule list can be spliced, so
 * `insertRule` and `deleteRule` exist — text in and a rule count out, with no
 * re-parse of the rest of the sheet and no notification of a style context,
 * which is stated as a boundary rather than implied. The CSSOM's document
 * binding — `styleSheets`, `getComputedStyle`, a parent stylesheet chain — is
 * deliberately absent, because it needs a DOM and the document layer is a peer
 * of RoboCSS.
 *
 * One capability joins the table — `cssom` — and one diagnostic domain
 * (`RoboCssDomainCssom`).
 *
 * 0.21.0 is the twentieth, and it is the third version added by Phase 18: it is
 * the **flexbox family**, which is the first property family here whose values are
 * three vocabularies rather than one. A flex container has a main axis
 * (`flex-direction`), a wrapping rule (`flex-wrap`) and a way of placing its items
 * and lines (`justify-content`, `align-items`, `align-self`, `align-content`), and
 * the module is those three closed sets plus the four facts a layout pass reads to
 * size the items (`flex-grow`, `flex-shrink`, `flex-basis`, `order`).
 *
 * The alignment set is the part worth reading, because it is the first vocabulary
 * here shared by more than one property in a way that is not merely a shorthand and
 * its longhands. CSS Box Alignment defines the four alignment properties as subsets
 * of one list of values, so this build has *one* enumerator set and *four* value
 * shapes; which values each property may hold is a column of the vocabulary's rows
 * rather than a comparison at a call site, and that is what makes
 * `align-items: space-between` an invalid value while `justify-content:
 * space-between` is a value. It is the same move `display` made in 9.12 — a closed
 * set whose classifications a consumer dispatches on — made once for four
 * properties instead of one.
 *
 * Two smaller facts are decisions rather than transcriptions. The two shorthands
 * `flex-flow` and `flex` are **carried declarations**, not vocabularies: each stands
 * for two halves of *different kinds* (`<flex-direction> || <flex-wrap>`, and
 * `none | <flex-grow> <flex-shrink>? || <flex-basis>`), so neither can take one
 * closed set the way `overflow` took its own, and nothing in this build expands a
 * shorthand. And `order` and `flex-grow`/`flex-shrink` are the two numeric shapes
 * the family adds — a signed integer and a non-negative number — kept apart because
 * their grammars differ and because the bound is on the factor and not the order.
 *
 * One capability joins the table — `flexbox` — and one diagnostic domain
 * (`RoboCssDomainFlex`). What the phase does *not* claim is flex layout: no line is
 * measured, no free space is distributed, and no main-axis position is computed,
 * because a value says what rule a layout pass follows rather than where the items
 * land.
 *
 * The surface will keep growing for the whole of Phase 9, so API freeze is
 * NOT claimed here (RoboCssApiFrozen=0).
 */

#ifndef ROBO_CSS_VERSION_H
#define ROBO_CSS_VERSION_H

#define RoboCssVersionMajor 0
#define RoboCssVersionMinor 21
#define RoboCssVersionPatch 0
#define RoboCssVersionString "0.21.0"

/** 1 once the Phase 9 surface is frozen (not yet — see header comment). */
#define RoboCssApiFrozen 0

#endif /* ROBO_CSS_VERSION_H */
