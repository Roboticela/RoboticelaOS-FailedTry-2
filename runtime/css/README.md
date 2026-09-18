# RoboCSS

Cascading style sheet subsystem for Roboticela Runtime (**through Phase 9.9,
colours**).

**Version:** 0.9.0 — `ROBO_CSS_API_FROZEN=0` (the surface grows for the whole of
Phase 9)

Public RoboCSS never depends on Chromium, and — per `DEPENDENCIES.md` — it is a
**peer of RoboHTML**, not a client of it. It has no knowledge of layout,
painting, rendering, windowing, graphics or display. Those layers sit above it
and consume it.

## Scope

**9.1 (architecture), 9.2 (tokenizer), 9.3 (parser), 9.4 (selectors), 9.5 (the
cascade), 9.6 (specificity), 9.7 (inheritance), 9.8 (computed values) and 9.9
(colours).** 9.1
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
│                             RoboCSSColor.h and RoboCSSUnit.h)
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
scripts/robo test-css        # this subsystem's 490 tests only
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
| `../PROGRESS.md` | The Goal 9.1–9.10 records and their definitions of done |
