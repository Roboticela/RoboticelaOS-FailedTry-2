# RoboCSS

Cascading style sheet subsystem for Roboticela Runtime (**through Phase 9.6,
specificity**).

**Version:** 0.6.0 — `ROBO_CSS_API_FROZEN=0` (the surface grows for the whole of
Phase 9)

Public RoboCSS never depends on Chromium, and — per `DEPENDENCIES.md` — it is a
**peer of RoboHTML**, not a client of it. It has no knowledge of layout,
painting, rendering, windowing, graphics or display. Those layers sit above it
and consume it.

## Scope

**9.1 (architecture), 9.2 (tokenizer), 9.3 (parser), 9.4 (selectors), 9.5 (the
cascade) and 9.6 (specificity).** 9.1 builds the model the rest of Phase 9 is
written against; 9.2 builds the first machine that fills it; 9.3 builds the
second — the grammar that consumes the token stream and produces that model out of
real CSS; 9.4 builds the third, which turns a rule's prelude into real selectors
and answers whether an element satisfies one; 9.5 is the first thing that
*applies* anything, by collecting what matches an element in cascade order; 9.6
supplies the third and last ordering key, so the cascade can now answer every
contest it can see:

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

`robo_css_slice_t` is a borrowed view. Every API that produces one documents who
keeps those bytes alive; no slice is ever freed by RoboCSS.

Module slots are static and bounded (`ROBO_CSS_MAX_STYLESHEETS`), so the module
has no hidden global allocator and `robo_css_shutdown` can release everything it
handed out. Stylesheet ids come from a monotonic cursor and are never reused, so
a destroyed id stays dead.

## Module layout

```text
runtime/
├── Include/RoboCSS/          public headers (one per model, plus RoboCSSTokenizer.h
│                             and RoboCSSSpecificity.h)
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
scripts/robo test-css        # this subsystem's 386 tests only
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
| `docs/Tests.md` | Suite map, what the cleared-record contract test exists for, the fixture rule, and how to run a focused suite |
| `../PROGRESS.md` | The Goal 9.1–9.6 records and their definitions of done |
