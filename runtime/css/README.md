# RoboCSS

Cascading style sheet subsystem for Roboticela Runtime (**Phase 9.1
architecture**).

**Version:** 0.1.0 — `ROBO_CSS_API_FROZEN=0` (the surface grows for the whole of
Phase 9)

Public RoboCSS never depends on Chromium, and — per `DEPENDENCIES.md` — it is a
**peer of RoboHTML**, not a client of it. It has no knowledge of layout,
painting, rendering, windowing, graphics or display. Those layers sit above it
and consume it.

## Scope

**9.1 (architecture) only.** This phase builds the model the rest of Phase 9 is
written against, and nothing that would interpret it:

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

A parsed stylesheet in this phase is one assembled by hand.
`robo_css_stylesheet_parse` answers `ROBO_NOT_SUPPORTED` and says so in the
message (`Goal 9.3`), leaves the rules and the state untouched, and records the
reason on the stylesheet so a caller that did not ask for it still sees it. **No stub pretends to parse.**

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
├── Include/RoboCSS/          public headers (one per model)
├── Source/RoboCSS/           production sources
└── css/
    ├── tests/                the nine suites and their shared fixtures
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
scripts/robo test-css        # this subsystem's 88 tests only
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
| `docs/Tests.md` | Suite map, what the cleared-record contract test exists for, the fixture rule, and how to run a focused suite |
| `../PROGRESS.md` | The Goal 9.1 record and its definition of done |
