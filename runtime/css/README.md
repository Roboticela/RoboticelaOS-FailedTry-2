# RoboCSS

Cascading style sheet subsystem for Roboticela Runtime (**through Phase 9.3
CSS parser**).

**Version:** 0.3.0 — `ROBO_CSS_API_FROZEN=0` (the surface grows for the whole of
Phase 9)

Public RoboCSS never depends on Chromium, and — per `DEPENDENCIES.md` — it is a
**peer of RoboHTML**, not a client of it. It has no knowledge of layout,
painting, rendering, windowing, graphics or display. Those layers sit above it
and consume it.

## Scope

**9.1 (architecture), 9.2 (tokenizer) and 9.3 (parser).** 9.1 builds the model the
rest of Phase 9 is written against; 9.2 builds the first machine that fills it;
9.3 builds the second — the grammar that consumes the token stream and produces
that model out of real CSS. Nothing here yet *interprets* what it builds:

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

`robo_css_get_capabilities` reports which of these subsystems exist, so a caller
never has to infer it from a version number.

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
| Stylesheet slot | Its rule list, its source copy, its last-error record | The source when borrowed, error messages (never freed) |
| Every `robo_css_slice_t` | Nothing, ever | — |

`robo_css_slice_t` is a borrowed view. Every API that produces one documents who
keeps those bytes alive; no slice is ever freed by RoboCSS.

Module slots are static and bounded (`ROBO_CSS_MAX_STYLESHEETS`), so the module
has no hidden global allocator and `robo_css_shutdown` can release everything it
handed out. Stylesheet ids come from a monotonic cursor and are never reused, so
a destroyed id stays dead.

## Module layout

```text
runtime/
├── Include/RoboCSS/          public headers (one per model, plus RoboCSSTokenizer.h)
├── Source/RoboCSS/           production sources
└── css/
    ├── tests/                the fourteen suites and their shared fixtures
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
scripts/robo test-css        # this subsystem's 208 tests only
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
| `docs/Tests.md` | Suite map, what the cleared-record contract test exists for, the fixture rule, and how to run a focused suite |
| `../PROGRESS.md` | The Goal 9.1, 9.2 and 9.3 records and their definitions of done |
