# RoboCSS

Cascading style sheet subsystem for Roboticela Runtime (**through Phase 9.15,
borders**).

**Version:** 0.15.0 — `ROBO_CSS_API_FROZEN=0` (the surface grows for the whole of
Phase 9)

Public RoboCSS never depends on Chromium, and — per `DEPENDENCIES.md` — it is a
**peer of RoboHTML**, not a client of it. It has no knowledge of layout,
painting, rendering, windowing, graphics or display. Those layers sit above it
and consume it.

## Scope

**9.1 (architecture), 9.2 (tokenizer), 9.3 (parser), 9.4 (selectors), 9.5 (the
cascade), 9.6 (specificity), 9.7 (inheritance), 9.8 (computed values), 9.9
(colours), 9.10 (units and numbers), 9.11 (the box model), 9.12 (display), 9.13
(positioning), 9.14 (backgrounds) and 9.15 (borders).** 9.1
builds the model the rest of Phase 9 is written against; 9.2 builds the first
machine that fills it; 9.3 builds the second — the grammar that consumes the token
stream and produces that model out of real CSS; 9.4 builds the third, which turns a
rule's prelude into real selectors and answers whether an element satisfies one;
9.5 is the first thing that *applies* anything, by collecting what matches an
element in cascade order; 9.6 supplies the third and last ordering key, so the
cascade can answer every contest it can see; 9.7 answers the question underneath
the cascade — what the element *has* for the properties nothing declared; 9.8 is
the first that **changes** a value rather than locating one, turning that resolved
token sequence into a length, a percentage, a colour or a keyword; 9.9 is the first
that gives one of those shapes a *representation* of its own, because a colour is
the one CSS value whose meaning is arithmetic and its arithmetic has to be shared:

| Goal | What 9.1 delivers | What it does **not** do |
|---|---|---|
| 9.1.2 source | Bytes plus ownership: borrowed string, borrowed memory, and an owned copy; byte offsets; 1-based line/column tracking; half-open ranges that slice the source | — |
| 9.1.3 token | The full CSS Syntax token vocabulary as an enum, with classification, bracket pairing and hash types | Tokenize |
| 9.1.4 value | Component values: `token`, `function`, `block`, `list`, with move-in children | Parse a value |
| 9.1.5 declaration | Name + value + `!important` + source ranges + state (`NONE`/`PARSED`/`INVALID`) | Resolve a property |
| 9.1.6 rule | Style rules and at-rules, with declaration and nested-rule storage | Match a selector |
| 9.1.7 stylesheet | Module slot identity, origin, state, attached source, rule storage and traversal | Parse a stylesheet |
| 9.1.15 selector | Simple (`type`/`class`/`id`/`attribute`/`pseudo`/`universal`), compound, complex and list selector records, with combinators and an almost-anything check | Match anything |
| 9.1.16 property | A property *table*: name → stable per-table identity, and an id → parser/computer hook registration | Ship a property database |
| 9.1.13 error | `recoverable` vs `fatal` vocabulary, distinct from `robo_result_t` | Recover from anything |
| 9.1.14 parser | A cursor over a source: offset, line, column, seek, advance, and the first-error record | Read a single token |
| 9.2.1 tokenizer | **The real CSS Syntax state machine**: source bytes → the 9.1 token vocabulary, one token per call, with lookahead, trivia skipping, accurate ranges and the first-error record | Parse the token stream |
| 9.3.1 parser | **The real CSS Syntax grammar**: token stream → rules, at-rules, declaration blocks and component values. Transactional, bounded, and recovering; the inline-style boundary is the same code path as a rule's block | Match a selector, order the cascade, compute a value |
| 9.5.1 cascade levels | The precedence table: origin × importance → one of eight levels, ascending, with `inline` above the origin it belongs to and the important half mirrored the way CSS mirrors it | Change (the table is final; specificity is a different key altogether) |
| 9.5.2 collection | **The real resolution door**: every origin's sheets, every matched rule's declarations, and the element's own `style` attribute, in cascade order, with a count of everything the build could not use | Apply a *value* |
| 9.5.3 resolution | The winner for one property, by level, then specificity, then document order | Compute a value |
| 9.6.1 specificity | **The dedicated third key**: a selector's three counts — ids, then classes/attributes/pseudo-classes, then types/pseudo-elements — computed from the same selector records matching reads, compared as a tuple and saturated rather than wrapped | Inherit or compute anything |
| 9.6.2 integration | The cascade consumes specificity through one comparison function and holds no arithmetic of its own; `:not()` is worth its argument; a rule's selector list is worth the member that matched | Guess when a selector matched nothing |
| 9.7.1 property registry | **Property metadata, once**: does a property inherit by default, and what is its initial value — stored as text *and* as the parsed component-value tree, with initial values parsed once at registration | Issue property ids or hold per-caller state (that is the property table) |
| 9.7.2 resolved style | **What one element resolves to**: one entry per property it has an answer for, each with its provenance (`DECLARED`/`PARENT`/`DEFAULT`/`DEFERRED`) and the keyword that produced it | Compute anything |
| 9.7.3 resolution | The three sets walked in order — the element's cascade winners, then the inherited-by-default properties the parent carries, then the registry's initial values | Inherit across a DOM (the parent is an argument) |
| 9.7.4 CSS-wide keywords | `inherit`, `initial` and `unset` obeyed on the parsed value; `revert` recognised, carried unresolved and **reported** | Treat a keyword as a string, or half-obey `revert` |
| 9.7.5 chain door | `robo_css_style_context_resolve_inherited_style`: walk up through the host's `parent`, resolve top down so the parent is the *immediate* resolved parent, bounded and reported | Walk a tree this module does not own, or cache between calls |
| 9.8.1 typed value | **The closed set of shapes** — `KEYWORD`, `NUMBER`, `PERCENTAGE`, `LENGTH`, `COLOR`, `FAMILY_LIST`, `PRESERVED`, `INVALID` — with the unit table (fixed factors convert, relative units are kept) and the colour forms this build can already express (the named set, `transparent`, all four hex lengths, `rgb()`/`rgba()` in both syntaxes) | Resolve against a containing block, a font or the element itself |
| 9.8.2 normaliser | `robo_css_computed_value_compute`: shape plus value in, typed value out. A value that means nothing for its property is an **answer** (`INVALID` with a recorded reason), not a failure code | Reparse a declaration, or key off a property *name* |
| 9.8.3 computed style | The 9.1 container made richer: each entry now carries its name, its typed value and a provenance bitset (`DECLARED`/`INHERITED`/`DEFAULT`/`DEFERRED`/`CUSTOM`/`UNRESOLVED`/`INVALID`), plus the incomplete and truncated counters | Drop the specified value, or hide that a style is unsettled |
| 9.8.4 chain door | `robo_css_style_context_compute_style`: resolve *and compute* each level top-down, so a child inherits the parent's **computed** value rather than a recomputation of what it specified | Compute a used value, or keep a cache |
| 9.8.5 real-tree integration | `css.dom.computed.*` drives the whole pipeline — tokenize, parse, match, specificity, cascade, inherit, compute — over the **visual checkpoint's own document**, and proves the rendered frame is byte-identical afterwards | Change what the demo paints |
| 9.9.1 colour record | **One colour, four kinds** — `NONE`, `RGBA` (8 bits per channel, sRGB), `CURRENT` (`currentColor`, contextual by construction) and `INVALID` — with kind-aware equality and no ownership at all | Interpolate, blend, or represent a space other than sRGB |
| 9.9.2 named set | All **148** standard names, ASCII case-insensitive, reachable by name *and* by index so a suite walks the whole table | Treat an unknown identifier as a colour, or fold `transparent` in (it is a keyword, not a name) |
| 9.9.3 syntaxes | `#rgb`/`#rgba`/`#rrggbb`/`#rrggbbaa`, `rgb()`/`rgba()` and `hsl()`/`hsla()` in both the legacy comma and modern space forms, with clamping rather than wrapping and hue normalised rather than clamped | Accept a mixed separator, or a six-digit spelling of a three-digit colour |
| 9.9.4 outcomes | Six answers — `RESOLVED`, `CURRENT`, `DEFERRED`, `MALFORMED`, `UNKNOWN_NAME`, `INVALID` — because the diagnostics differ and the diagnostic is the point | Call legal-but-unrepresentable CSS (`oklch()`) invalid, or guess at a colour it cannot compute |
| 9.9.5 resolution | `currentColor` resolved by the **computed style**, against the parent's `color` on `color` itself and the element's own `color` everywhere else, with the unresolved flag staying set when there is nothing to resolve against | Resolve `currentColor` to a default |
| 9.9.6 serialization | One stable spelling per kind (`none`, `invalid`, `currentColor`, lower-case hex with alpha only when it is not opaque) | Write a colour back as the name it arrived as, so two equal colours print differently |

## The tokenizer (9.2)

The tokenizer is a **by-value cursor the caller owns**, not a heap object, and it
allocates nothing at all — `init` cannot fail for an allocation reason, nothing
leaks, and one is as cheap to put on the stack as to embed in the parser it will
live in. `RoboCSSTokenizer.h` documents the three decisions that shape it:

1. **The stream is a cursor, not an object.** `init`, `dispose`, `reset`; no
   allocation anywhere in the subsystem.
2. **The caller's bytes are never rewritten.** The specification's preprocessing
   (CR/CRLF/FF → LF, NUL → U+FFFD) is replaced by *classification*, because a
   token's range is a byte range into the caller's own buffer and rewriting would
   make that range a lie. The one case classification cannot substitute — NUL,
   which preprocessing would have made an identifier character — is reported as
   the delimiter it really is, rather than claiming a value the bytes do not
   contain.
3. **A token is raw; its value is computed.** `text` stays the raw bytes so `text`
   and `range` can never disagree; escapes are resolved on demand through the
   `robo_css_token_value_*` accessors. So no token points into scratch storage, no
   token is invalidated by asking for the next one, and a token can be copied and
   kept for as long as its source lives.

Malformed CSS never crashes and never terminates the caller: a bad style sheet is
a **token plus a recorded error**, not a failed call. `docs/Tokenizer.md` has the
full architecture and the deterministic outcome table.

## The parser (9.3)

`robo_css_stylesheet_parse` is a **real parse** as of 9.3: it tokenizes the
attached source and runs the CSS Syntax structure rules over the result. It is
**transactional** (a parse builds into its own rule list and only a parse that
returned `ROBO_OK` replaces the sheet's rules, so parsing twice is idempotent
rather than additive), and it separates **the call's outcome** from **the CSS's
problems**: the return value is `ROBO_OK` whenever a model was built, *including*
one built by recovering, and `out_error` — always filled — is the first problem
recovered, or a record of kind `NONE` when the input was clean.

The grammar **lexes nothing**: every byte it sees arrives as a 9.2 token, and
every token it keeps is that same record — same type, same range, same hash
classification, same dimension unit. There is no path from the parser to the
source bytes except through a token, and `css.parse.integration.*` proves it by
re-lexing the source independently and requiring every leaf of the parse to match
a token at the same range with the same classification.

It **builds and interprets nothing**: a prelude is component values (so `selectors`
stays empty for 9.4), an at-rule is syntax and no more (`@media` and `@nonsense` go
through the same code, because there is no table to look a name up in), and
`!important` is *recorded* without ordering anything. Nesting is bounded at
`ROBO_CSS_MAX_NESTING` and past the bound a construct is **flattened, not
abandoned** — the tokens are still consumed, the rule is still produced, and only
its contents are dropped. `docs/Parser.md` has the full architecture, the
declaration and at-rule rules, the recovery boundaries and the range conventions.

9.4 adds the selector **grammar** and the **matcher**, and they are two separate
things. The grammar lexes nothing of its own: every name it records is a 9.2
token's *value*, escapes already resolved, interned once into the list's own name
arena — so `.foo\:bar` carries `foo:bar` and no matcher ever has to know escapes
exist. A selector list is **all-or-nothing**, as CSS says it is.

The matcher holds no DOM, no document, no cache and no stylesheet. It asks a
**host** — the four callbacks in `RoboCSSStyle.h` — about one element at a time,
which is what keeps RoboCSS a peer of RoboHTML rather than a client of it: nothing
in `Source/RoboCSS/` includes a RoboHTML header. Complex selectors are evaluated
right-most compound first and walked leftwards *with backtracking*, and the
recursion is bounded by the number of compounds in the selector rather than by the
tree, so the stack cost is a property of the stylesheet and not of the document.

Three outcomes, not two. `matched` and `unsupported` are independent, so a
negative is either *decided* or *undecided*: `h1 + p` against a host with no
sibling callback is undecided, while `:hover` parses, matches nothing, and names
itself in `condition`. What is supported is exactly 9.4's list — type, universal,
class, id, the six attribute comparators, the four combinators, and the seven
structural pseudo-classes — and everything else is reported rather than guessed
at. `docs/Selectors.md` has the full account.

## The cascade (9.5)

`robo_css_style_context_match_declarations` is a real collection as of 9.5. It
walks the context's sheets by origin, matches their rules through the same host,
parses the element's `style` attribute through the same grammar a rule's block
uses, and returns a `robo_css_cascade_t` — the declarations that apply, in
cascade order, each carrying its origin, its level, the sheet, rule and selector
it came from, and its document position.

The **level table** (origin × importance) is complete and final. Two rows are
decisions rather than transcriptions: inline sits above the origin it belongs to
(a `style` attribute is author-level with a specificity no selector reaches), and
the important half is *not* a plain reversal of the normal one — it reverses the
origins while inline stays directly above author, so an inline `!important` beats
an author `!important` and still loses to a user one.

The **ordering key** is three keys in one direction: level, then specificity, then
document order. 9.6 supplied the middle one, so resolution answers one property at
a time with a single verdict — a level decides first, a specificity gap decides
next, and the document position decides a genuine tie within one matched selector.
Everything the collection could not use is *counted* on the result — an undecided
selector, an at-rule block nothing evaluates, a sheet destroyed while referenced, a
rule whose prelude was never parsed as selectors — so `unsupported` on the result
is the difference between a lower bound and a wrong answer. `docs/Cascade.md` has
the full account.

## Specificity (9.6)

`RoboCSSSpecificity.h` is the third cascade key, and it owns *all* of the
arithmetic. A selector's specificity is three counts compared as a tuple:

```text
(A, B, C)
A = id selectors
B = class selectors + attribute selectors + pseudo-classes
C = type selectors + pseudo-elements
```

Three decisions shape it. It is **computed from the same records matching reads**,
so there is no second selector AST to keep in step, and no specificity is ever
reparsed from text. It is the specificity of **the member that matched**, not of
the rule's whole selector list, so `div, .card, #main` is worth `(0,0,1)` to a
bare `div` and `(1,0,0)` to the element the `#main` member matched. And it
**saturates rather than wraps**: each count is clamped at a represented maximum,
so an adversarial pile of `.a.a.a…` can never roll a highly specific selector back
into a low one.

`(A,B,C)` comparison is lexicographic and explicit — there is no single packed
integer to misread and no string comparison — and the ordering key has no other
implementation. `robo_css_get_capabilities` reports which of these subsystems
exist, so a caller never has to infer it from a version number. `cascade` and
`specificity` are both 1 for exactly the above.

## Inheritance (9.7)

The cascade says which declaration wins; inheritance says what the element *has*.
`robo_css_inherit_resolve` builds a **resolved style** from three sets, in the
order it reports them: the properties the element's own cascade declares (taking
the winner through the cascade's own resolver, never re-deciding precedence),
then the inherited-by-default properties the parent carries, then the registry's
initial values for the rest. The third set is deliberately only the
inherited-by-default properties — materialising `width: auto` everywhere would put
a value in a resolution that no input produced — so an element with no declarations
at all still resolves to a *complete* style.

A resolved style is a **pure function of its inputs**: the parent arrives as an
argument, nothing is cached between calls, and no global "current style" exists.
That is what makes it testable one element at a time and what makes its
invalidation story "re-resolve" rather than "remember to invalidate". The context
door (`robo_css_style_context_resolve_inherited_style`) walks up to the root and
resolves **top down**, so every level's parent is the *immediate resolved* parent —
`div { color: red } section { color: blue }` gives a `<p>` inside `<section>` blue
— and the walk is bounded and *reported* rather than silently truncated.

The CSS-wide keywords are obeyed here, on the parsed value and never on a string,
and a keyword is a single identifier and nothing else (`calc(inherit)` is not one).
`revert` is the deliberate exception: rolling it back means re-running a cascade
with one origin removed, so it is carried as a *deferred* entry, counted, and
reported through `unsupported` — never quietly resolved into something that looks
like an answer. Custom properties inherit always and have **no** initial value, so
an unset one simply has no entry. `docs/Inheritance.md` has the full account.

## Computed values (9.8)

Inheritance answers "what does the element *have*"; computation answers "what
*is* that". `robo_css_style_context_compute_style` resolves the ancestor chain and
computes each level before handing it to the level below, so an inherited value is
the parent's **computed** value — a length, a colour, a keyword — and a child of
`div { font-size: 16px }` holding `font-size: inherit` lands on `16px` as a typed
length rather than on the token sequence `16px`.

The normaliser is a function of **the value's shape and the value**, never of a
property name: the property registry supplies a `computation` column
(`PRESERVE`, `COLOR`, `LENGTH`, `KEYWORD`, `FONT_FAMILY`, `FONT_WEIGHT`,
`LINE_HEIGHT`) and the same seven shapes serve every property. That is what keeps
this stage free of context — no containing block, no font metrics, no parent — and
free of the caches a context-dependent stage would need.

Three kinds carry the decisions worth knowing about. **`PRESERVED`** is not
"unimplemented": `calc(100% - 20px)` *is* a computed value here, structurally
intact, because `%` needs a containing block that this stage does not have, and a
reduced number would be a lie. **`INVALID`** is an answer rather than an error
return — `color: nonsense` means nothing for `color`, so the call succeeds, the
value says INVALID, a diagnostic carries the reason and the style counts it —
because a failure code would force a caller to drop the property or invent a
value. **`UNRESOLVED`** is a flag that no kind can express: `var(--x)` and
`currentColor` are carried preserved *and* flagged, since a consumer reading a
reference as an ordinary token list would be reading it as a colour.

What is deliberately left to later phases: used values (`50%` stays `50%`), font
resolution (`font-size: medium` stays the keyword), `var()` substitution, shorthand
expansion, and a default stylesheet. `docs/ComputedValues.md` has the full account.

## Colours (9.9)

A colour is the one CSS value whose *meaning* is arithmetic, so 9.9 gives it a
module (`RoboCSSColor.h`) rather than letting the computed-value layer keep the
parser it had to carry in 9.8. The value layer now consumes that one parser instead
of holding its own — which is what keeps `rgb(255, 0, 0)` and `#ff0000` from ever
disagreeing about what they are.

The record is a **kind**, not just channels:

```c
typedef struct robo_css_color {
    robo_css_color_kind_t kind;   /* NONE | RGBA | CURRENT | INVALID */
    uint8_t red, green, blue, alpha;
} robo_css_color_t;
```

and that kind is what makes `currentColor` expressible. It is not a colour: it is a
reference to whatever `color` turns out to be, and CSS keeps it unresolved through
the computed-value stage for exactly that reason. Resolving it early — to black, or
to anything else — would make `color: currentColor` mean a fixed colour, which is
the one thing it must never mean. So it survives the value layer as a `COLOR`
value carrying the `UNRESOLVED` bit, and the computed style resolves it at the only
point that has the answer, by **two rules**:

- on `color` itself, `currentColor` is the **parent's** computed `color` — "the
  colour I would have inherited", which is the only reading that terminates;
- anywhere else, it is the element's **own** computed `color` — `background-color:
  currentColor` is "my text colour".

`color` is therefore resolved first, in a pass of its own, and everything else
afterwards against the settled result, so nothing can read a value that has not
been settled. When there is no parent computed style — a root element — the colour
**stays** contextual and **stays** flagged, because inventing black is the mistake
the keyword exists to prevent.

The sixth answer is the one that keeps this build from lying. `DEFERRED` means
"legal CSS this build has no representation for" — `hwb()`, `lab()`, `lch()`,
`oklab()`, `oklch()`, `color()`, `color-mix()`, `device-cmyk()` — and such a
declaration is carried with its tokens and flagged, not refused. `MALFORMED`,
`UNKNOWN_NAME` and `INVALID` all end as an invalid computed value and are separate
because a developer reading "rgb() arguments are out of range or malformed",
"unknown colour name" or "not a colour" learns three different things.

`var()` in a colour position is the fourth case, and it is settled *before* the
colour parser is asked: a reference is not a colour yet, so `color: var(--brand)` is
carried and flagged unresolved exactly as it is for every other shape. Nothing
substitutes `--brand`, and nothing reads `--brand` as a colour — a custom property
holding `#336699` keeps those five tokens.

`background-color` joined the property registry in this phase. The reason is not
backgrounds: it is the only *second* colour-shaped property, and without one the
rule that resolves `currentColor` against the element's own `color` would be
unreachable and therefore unprovable. `docs/Colors.md` has the full account.

## Units and numbers (9.10)

Most of what a declaration says is a number with something attached to it, and this
phase is the something. It is a module (`RoboCSSUnit.h`) for the same reason
colours got one: 9.8's unit table lived inside the computed-value layer because that
was its only reader, and a second reader — whatever has a font size, or a viewport —
is what turns a table into a module. The value layer now consumes that vocabulary
rather than keeping its own.

The unit set is a **closed vocabulary**, not strings. `10px` is not `10` plus the
text `px`: the unit is an enumerator, and one table row says everything about it —
its spelling, its family, its fixed factor, and which viewport and dimension it
measures. Adding `dvw` is adding a row rather than adding a branch to four
functions. `10PX` is `10px`; `10pixels` is not.

The decision the phase is built around is that resolving a length is a **question**
rather than a multiplication:

```c
ROBO_CSS_LENGTH_RESOLVED         *out_px holds the length
ROBO_CSS_LENGTH_NEEDS_CONTEXT    the unit is fine; the context is not
ROBO_CSS_LENGTH_NOT_A_LENGTH     NONE, unknown, or not a length
ROBO_CSS_LENGTH_INVALID          a magnitude or context input that is not finite
```

`1in` is 96px *by definition*, so absolute conversion is exact and consults no
display — nothing here knows about DPI, because only a used value may. `2em` is not a
length until something says what the font size is, and a module that guessed would
produce a number that looked computed and was wrong. So `robo_css_unit_resolve_px`
takes a **context** — an argument, never a global, with a `has_` flag per fact, so
that "the font size is 16" and "I do not know the font size" stay two different
answers — and answers `NEEDS_CONTEXT` when the context does not say enough.

`NEEDS_CONTEXT` is the answer that matters. Collapsing it into "failed" is how a
caller ends up substituting a default it invented, which is the mistake this module
exists to prevent. `0em` with no context is still `NEEDS_CONTEXT`: what is missing is
the context, not the magnitude.

The type is four kinds and no fifth:

```c
typedef struct robo_css_numeric {
    robo_css_numeric_kind_t kind;   /* NONE | NUMBER | INTEGER | PERCENTAGE | LENGTH */
    double number;
    robo_css_unit_t unit;           /* LENGTH only */
} robo_css_numeric_t;
```

Separate because CSS's grammar separates them: a property whose grammar says
`<integer>` must refuse `1.5`, and must refuse `1.0` too — because `1.0` is a number
token, and accepting it would be the silent coercion the distinction exists to
catch. There is no `INVALID` kind: a value that is none of the four is not a numeric
value, which is a fact about the parse rather than about the value.

The parse has five outcomes, and `DEFERRED` is the one that keeps the phase honest.
`calc(100% - 20px)` and `var(--w)` are legal CSS that is not a number yet, so they
are carried with their tokens rather than called mistakes or half-evaluated.
`UNKNOWN_UNIT` (`10furlongs`) and `NON_FINITE` (`1e999`) are separate from `INVALID`
(`red blue`) because a developer learns three different things.

One canonical spelling, so two equal values cannot print differently: `10` not
`10.000000`, `1.5` not `1.500000`, `-0` → `0`, `10PX` → `10px`, and a magnitude
below the fixed-point precision rendered compactly rather than as `0` — because
printing a value that is not zero as zero is a lie a layout would act on.

`docs/Units.md` has the full account.

## The box model (9.11)

A box is four edges, and CSS writes them twice — four longhands, or one shorthand —
so something has to turn the second into the first. Everything downstream is written
against the longhands, which is why `margin: 10px 20px` has to *become* four values
rather than stay one declaration nothing can consume.

This phase is the first whose subject is a **property family** rather than a
representation, and its module (`RoboCSSBox.h`) owns only the vocabulary: the four
sides in CSS's own order, the three shorthands this build distributes (`margin`,
`padding`, `border-width`) with the longhand each side stands for, and the 1/2/3/4
component mapping as a table with one row per arity. `border-width` is distributed
although nothing paints a border, because the four widths are the four values;
`border` is *not*, because it stands for four widths, four styles and four colours and
this build represents one of three.

The order of the sides is load-bearing rather than conventional. `1px 2px 3px` means
"top, then both horizontal, then bottom", which is a statement *about the order*, and
a set that listed the sides differently would transpose every three-value shorthand.

Two values refuse to expand, and they refuse for different reasons. Five components is
an *invalid declaration* the grammar accepted because validity is not syntax. A
`var()`/`env()` reference is a legal declaration whose component count is not known
until substitution — which happens *before* expansion in CSS — so splitting it now
would assign the whole reference to every side. `calc()` needs no such care, and the
suite pins both halves of that distinction.

The decision the phase is built around is that **expansion is not resolution**. Which
declaration wins an edge is the cascade's answer, including when the two carry
different property names — `margin: 10px; margin-left: 20px` is level, then
specificity, then document order, asked across two names instead of within one. That is
why it is asked of the cascade (`robo_css_cascade_candidate_compare`) and why nothing in
the box model weighs a level or compares a specificity. A distributed longhand is then
resolved like any other declaration, so `margin: inherit` and `margin-left: inherit`
mean the same thing to all four edges *by construction* rather than by two
implementations agreeing.

Three value shapes join the computed layer, and they differ by **vocabulary**:
`auto` is a margin and is not a padding, `medium` is a border width and is not a
margin. Sharing one body and differing only in the grammar they are handed is what
makes `padding: auto` an INVALID computed value rather than a keyword a later phase
would have to interpret. Fourteen properties join the registry — the twelve longhands,
`border-width` and `box-sizing` — and none of them inherits.

A shorthand that cannot be distributed is *reported* rather than guessed at: the
author's text stays on the shorthand's own entry, the count is on the resolved style,
and the resolution is marked incomplete. That is the phase's one quiet failure, and a
consumer that could not see it would read a declaration and find nothing underneath it.

Nothing here is layout: no edge is measured, no margin collapses, `50%` stays a
percentage and `auto` stays the keyword. `docs/BoxModel.md` has the full account.

## Display (9.12)

`display` is the one property every later phase reads on every element, and it is not a
word. `display: block` says three things at once — a box is generated, it is block-level,
and its contents are in flow — while `display: none` says the first of those is *false*.
A build that modelled `none` as zero geometry would be wrong in a way no test of a
rectangle would catch, so the value carries the three facts separately, as the three CSS
Display 3 names for them: the value, its **outer** display and its **inner** display.

`inline-block` is the row that earns the second column. CSS defines it as
`inline flow-root`, and this table says the same thing — an inline outer with a
formatting context of its own — so `is_inline_block` is derived from two columns rather
than kept as a flag beside them. The columns are also where the *next* values arrive:
`flex` is a block outer with a flex inner, `inline-grid` an inline outer with a grid one,
and `contents` a value that generates no box while being emphatically not `none`.

The phase's decision is that the vocabulary is **closed**. `display` moved off the
generic `KEYWORD` shape — which accepts any identifier, right for `cursor: grab` and
wrong here — onto one of its own, and the difference is a single branch: ask the
vocabulary. So `display: bogus` is a mistake with a diagnostic rather than a keyword
carrying the text `bogus`, and the four refusals stay four sentences because `flex`
deserves a better answer than `bogus`.

`display` does not inherit, and that leaves a hole a consumer has to know about: 9.7
materialises initial values for the inherited-by-default properties only, so an element
that declares nothing has *no entry* for `display`. The answer still exists — the
property's initial value — and the door that holds it is the style context's
`robo_css_style_context_query_computed_value`, which returns the value beside a
`from_initial` flag. That is why this phase adds no accessor to the computed-style
object: the container cannot answer the common case, and a door that answers less than
one beside it is worse than no door.

The multi-keyword syntax and the values this build has not added are **refused** rather
than approximated. HTML's own defaults are not here either: a `div` is not block, because
that is a user-agent *stylesheet* and this is a vocabulary. And a computed `display: none`
is a fact a later phase acts on rather than an action this one takes — no element leaves a
tree, because there is no tree yet.

`docs/Display.md` has the full account.

## Positioning (9.13)

`position` decides what an element's box is positioned *relative to*, and its five
answers are not five sizes of one behaviour. `static` is not positioned at all —
its `top` is ignored — `relative` is in flow and shifted from where flow put it,
`absolute` and `fixed` are lifted out of flow and placed against an ancestor or the
viewport, and `sticky` is in flow until a scroll constraint moves it. Modelling
that as one identifier in one field is how `top: 10px` comes to move a `static`
element.

So the vocabulary is a table with one row per value, and each row carries the two
facts the value *is*: whether the box stays in **flow** (the layout question — does
it take up room in its parent) and whether it is **positioned** (the containment
question — what does an offset inside it resolve against). The two are not each
other's opposite. `relative` and `sticky` are both in flow *and* positioned, which
reads as a contradiction until you notice they are two questions, and they are two
columns rather than one three-valued kind because they are asked separately and by
different stages.

The phase's second subject is the four insets, and its decision is that they have
**no grammar of their own**. `top`, `right`, `bottom` and `left` are
`<length-percentage> | auto` — the box model's `EDGE_OFFSET` shape exactly — so they
join that family and reuse its normalisation, which is what keeps `top: 20%` and
`margin-top: 20%` from ever disagreeing. Their *names* come from the side
vocabulary, and the phase's suite walks the family over `robo_css_box_side_count()`
rather than hand-listing the four, so a row written against the wrong side cannot
pass.

`z-index` is the first property whose value must be an **integer**, and it refuses
`0.0` even though the magnitude is whole — `0.0` and `0` are the same number and
different tokens, and CSS's `<integer>` grammar is about the token. Accepting it
would be a silent success in the one place the distinction exists to catch. `auto`
stays `auto`, because "takes no part in its parent's stacking decisions" is a
different statement from "at level zero".

The two closed vocabularies share one case body: the vocabulary, its two doors and
its refusals are data in a row, so a third closed vocabulary is a row in that table
rather than a third copy of "ask the identifier, then ask the table". The same
discipline made 9.12's `display` and this phase's `position` arrive as two rows of
the same shape.

Nothing here is layout: no containing block is found, no offset is resolved, no
rectangle exists, and no stacking order is computed. `position: sticky` is a value
and not a behaviour — refusing it would lose the offset values beside it, and
carrying it is exactly what it is before anything scrolls. `docs/Positioning.md`
has the full account.

## Backgrounds (9.14)

A background is the first property *family* whose parts are not the same kind of
thing. The box model's four edges are four of one value; `background` is one
declaration that speaks for six properties of six different shapes — a colour, an
image reference, a repeat style, a pair of positions, a pair of sizes and an
attachment — and no two of those have the same grammar. So its shorthand is not a
positional mapping: a component goes to whichever longhand its **kind** names, and
that classifier is the module's centre.

The two closed vocabularies join the shared grammar rather than growing their own.
`background-repeat` (`repeat`, `repeat-x`, `repeat-y`, `no-repeat`) and
`background-attachment` (`scroll`, `fixed`) are rows in the table `display` and
`position` already use, so their three refusals come in the same three sentences and
none of that code is written twice. `repeat-x` is not a fifth behaviour — it is
"tiles horizontally, does not tile vertically" — so the row carries the two *axes*
and `repeats_on_axis` is derived from it, exactly as the display row carries an
outer and an inner display.

The position pair is the first shape whose value is *two* components, and the pair
is the value because CSS's single-component forms are folded into it: `center`
computes to `center center` and `top` to `center top`, since a position keyword is
spelled by its **axis** — `left` is the START edge horizontally and `top` is the
START edge vertically. `left 10px` is the edge-relative form, a different grammar
whose *order* carries the meaning, and it is refused rather than half-read.
`background-size` is the same pair shape with three whole-value keywords beside it,
and a pair of two `auto`s folds back to `auto` because they are one computed value
with one spelling.

What the shorthand does to a longhand it did not name is the phase's one
behavioural decision, and it is the *opposite* of the box model's: a box edge the
author did not write has no value, while a background longhand the author did not
write is **reset to its initial value**, because the shorthand stands for all six.
That is what makes `background: red` mean "just this" on an element an earlier rule
gave an image.

Nothing paints. No image is loaded, no percentage is resolved against a box, and
multiple background *layers* are refused with a reason rather than half-read. What
is converted is the one thing that is exact — an absolute length arrives as the
reference pixel, so `background-position: 1in` and `margin-top: 1in` cannot come to
disagree about what a computed length is. `docs/Backgrounds.md` has the full
account.

## Borders (9.15)

A border is the first subject that is a **grid** rather than a property or a family. Three
kinds — width, style, colour — over four sides is **twelve longhands**, and no fewer than
**four** declarations can set any one of them: the longhand, its positional row
(`border-width`), its side shorthand (`border-top`) and `border` itself. That single fact is the
phase.

A longhand two declarations can speak for cannot be materialised by two independent passes, and
9.11 had put `border-width` on the box model's pass with a note that it was provisional. So the
phase's central act is a **move**: the distribution of the three border rows leaves the box
model's pass for one that can see all four declarers at once, and the box pass now asks
`robo_css_border_distributes_shorthand` before it distributes a row — the question is not
"is this a positional shorthand" but "is this row yours or mine". What stayed is the **table**,
and it had to: a row is where the relation between a shorthand, its four sides and the twelve
names is written down once, so `robo_css_border_longhand_name` reads it rather than spelling
`border-top-width` a second time. The same move grew the table by two rows — `border-style` and
`border-color` had no row because nothing before this phase spread them over four sides.

The style vocabulary is the fifth closed set and a row in the table the other four use, so a
sixth is a row. What is new is the **classifier**: a composite shorthand's components map to a
*kind* rather than to a position, and the order the questions are asked in is the whole logic —
a style keyword before a width keyword before a colour *name*, because `none` and `hidden` are
styles, `thin`/`medium`/`thick` are the only identifiers left that are certainly not colours,
and `red` can be settled by nothing else. A `var()` is a fourth answer rather than a guess.

The one value that is a reference is the one worth remembering: `border-color`'s initial value
is **`currentcolor`**, so it is *not* `transparent` the way `background-color` is. A border whose
colour had been resolved at registration time would be a border nobody could see and no author
could explain. The reference survives to the layer with an element, and the two doors that reach
a computed value — the pass and the query — resolve it the same way, so an element that declared
no border colour and one whose shorthand *reset* the colour agree, because they mean the same
thing.

Two behaviours differ from the two families beside it, and each is CSS's own: a kind a composite
did not name is **reset to its initial value**, as a background longhand is, while a *positional*
row has no such case because one to four components always cover all four sides. Nothing paints,
no edge is measured and `border-radius` is not a value this build has. `docs/Borders.md` has the
full account.

## The one rule worth reading first

RoboCSS's syntax is modelled as **by-value records** (a value, a token, a
declaration, a selector, a rule) rather than as opaque pointers, because they are
inert description with no external identity. The price is one rule, stated once in
`RoboCSSTypes.h` and depended on everywhere:

> A record must be **cleared** before any call that may release what it already
> holds. Every `make`/`init`/`append`/`dispose` call does exactly that.

Those calls release first and then set, so reconfiguring a record never leaks what
it held before. That is also why an *uninitialized* record is not safe: the release
step reads fields that still hold whatever was on the stack.

`ROBO_CSS_CLEARED` is how a record is declared cleared in one line. The cleared
state is the all-zero byte pattern *by design* — every field's zero is its empty
value — so declaring with the macro and declaring then calling the matching init
function produce the same record. `css.records.cleared_state` asserts that
equivalence field by field.

## Ownership

| Owner | Owns | Does not own |
|---|---|---|
| `robo_css_source_t` | Its bytes only in `KIND_COPY` | Borrowed bytes, labels (copied into the record) |
| `robo_css_value_t` | Its children array and every child, recursively | Its token, its slices |
| `robo_css_declaration_list_t` / `robo_css_rule_list_t` / selector collections | Their element arrays and every element moved into them | — |
| `robo_css_cascade_t` | Its candidate array and the declaration list parsed from the element's `style` attribute | The declarations it borrows from the context's sheets |
| Stylesheet slot | Its rule list, its source copy, its last-error record | The source when borrowed, error messages (never freed) |
| Every `robo_css_slice_t` | Nothing, ever | — |
| `robo_css_specificity_t` | Nothing, ever — it is three counts by value | — |
| `robo_css_property_registry_t` | Its definitions, their names, their initial-value text *and* the parsed tree | Property ids (it issues none) |
| `robo_css_inherited_style_t` | Every entry's name and value, both copied | The cascade, the parent style, the sheets the values' bytes come from |
| `robo_css_computed_value_t` | Its `text` and its `preserved` tree, and every byte inside them | Slices *within* a preserved tree, which borrow the source that declared them |
| `robo_css_computed_style_t` | Every entry's name, specified value, typed value and flags | The sheets, sources and parents those values were derived from |
| `robo_css_color_t` | Nothing, ever — it is four bytes and a tag by value | — |
| `robo_css_box_shorthand_t` | Nothing, ever — the three rows are `static const` | — |
| `robo_css_background_position_t` / `_size_t` | Nothing, ever — a pair holds two components by value | — |

`robo_css_slice_t` is a borrowed view. Every API that produces one documents who
keeps those bytes alive; no slice is ever freed by RoboCSS.

Module slots are static and bounded (`ROBO_CSS_MAX_STYLESHEETS`), so the module
has no hidden global allocator and `robo_css_shutdown` can release everything it
handed out. Stylesheet ids come from a monotonic cursor and are never reused, so
a destroyed id stays dead.

## Module layout

```text
runtime/
├── Include/RoboCSS/          public headers (one per model, plus RoboCSSTokenizer.h,
│                             RoboCSSSpecificity.h, RoboCSSPropertyRegistry.h,
│                             RoboCSSInherit.h, RoboCSSComputedValue.h,
│                             RoboCSSColor.h, RoboCSSUnit.h, RoboCSSBox.h,
│                             RoboCSSDisplay.h, RoboCSSPosition.h and
│                             RoboCSSBackground.h)
├── Source/RoboCSS/           production sources
└── css/
    ├── tests/                this subsystem's suites and their shared fixtures
    └── docs/                 this subsystem's design record
```

The plan's `source/ tokenizer/ parser/ syntax/ selector/ declaration/
stylesheet/ cascade/ computed/` split is **not** reproduced as directories. The
repository's convention is one flat module directory with a `Robo*`-prefixed
translation unit per concern, which keeps the build's explicit source list
readable and avoids ten directories holding one file each. The separation of
concerns the plan asks for is in the headers, which is where a reader looks for
it.

## Building and testing

```bash
scripts/robo test-css        # this subsystem's 573 cases only (`--filter css.`)
scripts/robo test-runtime    # the whole runtime suite
scripts/robo check           # build + test + validate
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test -Sanitize
```

The production runtime is gated on RoboCSS staying Chromium-free and free of any
higher runtime layer: `-Verify` runs `Assert-RoboCSSChromiumIndependence` over
`Include/RoboCSS`, `Source/RoboCSS` and `css/`.

## Documentation

| Document | Contents |
|---|---|
| `docs/Architecture.md` | Module boundaries, ownership and lifetime rules, every model, and the DOM / inline-style / default-stylesheet boundaries |
| `docs/Tokenizer.md` | The 9.2 tokenizer: the three decisions, token categories, source ranges, malformed-input outcomes, the stream API, lifetime, and how to run its tests |
| `docs/Parser.md` | The 9.3 parser: the two entry points, what the grammar is, nesting and the bound, declarations and `!important`, at-rules as rules *or* descriptors, the recovery boundaries, source-range conventions, lifetime, and the integration properties |
| `docs/Selectors.md` | The 9.4 selectors: the two doors, the name arena, the all-or-nothing rule, the four matcher levels and their result, how a complex selector is walked, the two facts about a relation, what is supported and what is deliberately not, the host seam, lifetime, and how to run the suites |
| `docs/Cascade.md` | The 9.5 cascade: the level table and the two decisions it encodes, the collection walk and what every counter means, cascade order and the stable sort that maintains it, resolution by the three keys, the inline boundary's three answers, what 9.5 deliberately does not do, lifetime, and how to run the suites |
| `docs/Specificity.md` | The 9.6 specificity: the `(A,B,C)` representation and why it is a tuple, the comparison, the per-level rules (universal zero, `:not()` worth its argument, pseudo-elements vs pseudo-classes), selector-list behaviour, saturation and its bounds, the cascade integration, and how to run the suites |
| `docs/Inheritance.md` | The 9.7 inheritance: the three sets and their order, the property registry and its precedence, the CSS-wide keywords and why `revert` is carried rather than obeyed, custom-property behaviour, provenance, ownership, the primitive and chain doors, the invalidation boundary, and the stylesheet-lifetime rule the suites follow |
| `docs/ComputedValues.md` | The 9.8 computed values: specified / resolved / computed / used, the closed set of typed shapes and why `PRESERVED`, `INVALID` and `UNRESOLVED` each exist, the unit model, the colour forms and the 9.9 seam, the registry's new `computation` column, the computed style's flags and counters, the chain door, determinism and ownership, and the invalidation boundary |
| `docs/Colors.md` | The 9.9 colours: the record and its four kinds, why 8 bits per channel, the 148-name table and why `transparent` is not in it, every syntax with its clamping and normalisation rules, the six outcomes and why each exists, the two `currentColor` rules, the `var()` boundary, the registry's second colour property, serialization, the deferred spaces as extension points, and the robustness set |
| `docs/Units.md` | The 9.10 units and numbers: the closed vocabulary and its four families, why `is_absolute` is asked of the table, the reference-pixel relationships, the context and its flags, why the small/large/dynamic viewport is a fallback chain, the four resolutions, the four numeric kinds and the two integer questions, the five parse outcomes and why there is no `MALFORMED`, the canonical spelling, the deferred `calc()`/`var()` boundary, the computed-value integration, ownership, and numeric safety |
| `docs/Tests.md` | Suite map, what the cleared-record contract test exists for, the fixture rule, and how to run a focused suite |
| `docs/BoxModel.md` | The 9.11 box model: the four sides and why their order is load-bearing, the three shorthands (and why `border` is not one of them), the 1/2/3/4 mapping as a table, the two refusals and why a `var()` is not a five-value shorthand, the three value shapes that differ by vocabulary, the fourteen registry rows, why expansion is not resolution, where the pass runs, what happens when a shorthand cannot be distributed, and the layout boundary |
| `docs/Display.md` | The 9.12 display: the four values and why the set is closed, the outer/inner halves and why `inline-block` is not "inline with a flag", the two classification questions and how `contents` will separate them, reading an identifier and the `none`-vs-unknown ambiguity, the shape's four refusals, the multi-keyword syntax and HTML defaults as drawn boundaries, the registry row and why its initial value is checked as vocabulary text, non-inheritance and the hole it leaves, and the query door a consumer uses |
| `docs/Positioning.md` | The 9.13 positioning: the five values, flow and positioned as two separate questions, the three classification doors and why an unknown enumerator answers 0 to all of them, reading an identifier and the `static`-vs-unknown ambiguity, the shape's three refusals and the reference boundary, why the four insets reuse the box model's `EDGE_OFFSET` and take their names from the side vocabulary, `z-index` and why `0.0` is refused, the six registry rows, non-inheritance and the hole it leaves, sticky as a value rather than a behaviour, and the layout/stacking boundary |
| `docs/Backgrounds.md` | The 9.14 backgrounds: the six longhands and why their order is load-bearing, the repeat rows' two axes and the attachment set, the edge vocabulary a keyword is spelled by, the position pair and every folding rule with its refusals, the size pair and why `auto` is a flag, `background-image`'s carried references, the classifier and its eight refusals, why the distribution *resets* while the box model's does not, the registry rows, non-inheritance and the hole it leaves, the paint/layer boundary (including why a hex colour in the shorthand is classified), and what the phase found in its own tests |
| `docs/Borders.md` | The 9.15 borders: the grid rather than a family, why the twelve longhands have one owner and the distribution had to *move*, what stayed in the box table and why, the ten-value style set and `none`'s trap, the three kinds, the classifier and why the order of its questions is the whole logic (including why a percentage is a width), the composite/positional split over reset semantics, the four declarers the cascade orders, `currentcolor` as the only initial value that is a reference and where the query door resolves it, the fourteen registry rows, the layout/paint boundary, ownership, and what the phase found in its own tests |
| `../PROGRESS.md` | The Goal 9.1–9.15 records and their definitions of done |
