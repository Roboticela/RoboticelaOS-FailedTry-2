# RoboCSS Architecture (Phase 9.1–9.2)

This is the design record for RoboCSS as it exists after Goal 9.1. It documents
boundaries and rules, not plans: everything described here is compilable code
with tests behind it, and everything *not* described here is a boundary that
answers `ROBO_NOT_SUPPORTED`.

## 1. Position in the runtime

```text
RoboLayout / RoboPaint / RoboRender / RoboCompositor      (above, later phases)
        ↑
RoboHTML  ←  peers  →  RoboCSS
        ↑
RoboDOM                                                   (the meeting point)
        ↑
RoboWindow → RoboGraphics → RoboPlatform → ... → RoboBase
```

RoboCSS may depend on RoboBase, RoboMem and RoboContainers today, and on the
RoboDOM boundary once it exists. It must **not** depend on RoboHTML, RoboLayout,
RoboPaint, RoboRender, RoboCompositor, RoboWindow, RoboGraphics, RoboDisplay or
Chromium. This is not a convention: `Assert-RoboCSSChromiumIndependence` fails
the production build if an include in `Include/RoboCSS`, `Source/RoboCSS` or
`css/` reaches any of them.

RoboCSS and RoboHTML are peers. They meet at the DOM, and neither includes the
other — CSS reads an element through a view that the *host* constructs, never
through a RoboHTML handle.

## 2. Records, not handles

RoboHTML identifies nodes with generation-checked handles because a node has
identity: it is created, mutated, moved and destroyed while other code holds
references to it. A CSS value, token, declaration or selector has no such
identity. It is inert description of a stylesheet's text, and nothing outside the
subsystem ever holds a reference to one across a lifetime.

So RoboCSS models them as **by-value records**. The consequence is the one rule
the whole subsystem rests on (§3). The alternative — an opaque handle per value —
would have meant an allocation, a slot table and a lifetime per token, which is
the wrong shape for a model whose ceiling is "twelve fields describing `10px`".

Sheets are the exception that proves the rule. A stylesheet *does* have identity
("this is the author sheet"), can outlive the code that made it, and is
referenced by id from a style context — so stylesheets get an id from a bounded
static slot table, a monotonic cursor, and a `destroy` that makes the id
permanently dead.

## 3. The record-lifetime rule

Every `make`/`init`/`append`/`set`/`dispose` entry point **releases what the
record already holds and then sets**. This is what makes reconfiguring leak-free:

```c
robo_css_value_t v = ROBO_CSS_CLEARED;
robo_css_value_make_list(&v, NULL);          /* v is a LIST */
robo_css_value_append(&v, &child, NULL);     /* v owns one child */
robo_css_value_make_function(&v, name, r, NULL);
                                             /* the child array is released here,
                                              * not leaked */
```

The price is a precondition, stated once in `RoboCSSTypes.h` and asserted by
`css.records.cleared_state`: **a record must be cleared before its first use.**
`ROBO_CSS_CLEARED` is the all-zero state, and every field's zero is its empty
value — each `*_NONE` enumerator is 0, an absent slice is `NULL/0`, an empty range
is `0/0`, an empty collection is `NULL/0/0`. So

```c
robo_css_value_t a = ROBO_CSS_CLEARED;
robo_css_value_t b;
robo_css_value_init(&b);
```

produce the same record. Zeroing by any other route (memset, a caller's own
initializer) is equally valid. Only leaving a record uninitialized is not.

Why this is not merely a documentation preference: if a record reaches a release
call uninitialized, the release step walks a `count` that holds stack garbage and
frees a pointer that was never an allocation. This was observed during 9.1 and is
exactly the class of defect the rule exists to make impossible to write by
accident.

## 4. The source model

`robo_css_source_t` answers "what bytes did we get, and where are we in them".

| Kind | Meaning | Owning |
|---|---|---|
| `NONE` | Nothing was ever configured | — |
| `EMPTY` | Configured, and there is nothing to read | — |
| `MEMORY` | Borrowed bytes plus an explicit length | no |
| `STRING` | Borrowed NUL-terminated string | no |
| `COPY` | A copy RoboCSS allocated | **yes** |

The `NONE`/`EMPTY` distinction is deliberate and the same one RoboHTML's
attribute model makes: "we looked and there is nothing here" is a different fact
from "nobody ever looked". `set_memory` and `set_string` collapse a zero length to
`EMPTY` rather than recording a borrow of nothing, because the caller's pointer
would be meaningless to keep.

`MEMORY` and `STRING` describe the same bytes with different guarantees about
them. `set_string` records `STRING`: a kind the model defines but no setter can
produce is a kind that does not exist, and a later phase reading `kind` to decide
whether it may assume a terminator would have been reading a lie.

Positions are byte offsets plus 1-based line/column. A CR, an LF or a CRLF pair is
exactly one break. **Columns count Unicode scalar values, not bytes**, so a
diagnostic column means the same thing in an ASCII stylesheet and in one written
in Arabic or Japanese.

Ranges are half-open `[begin, end)`. An empty range is legal and is the cursor's
own marker; the empty range at `length` is the position at EOF. Slicing a valid
range yields a borrowed slice pointing into the source's own bytes, so a
diagnostic that prints a range costs no allocation.

Ill-formed UTF-8 is reported (`is_valid_utf8`) and preserved — never repaired.
4.x's transcoder is not consulted here, and a nested source is not normalised.

## 5. The models

### Token

`robo_css_token_t` is the full CSS Syntax vocabulary as an enum — identifier,
function, at-keyword, hash (id / unrestricted), string, bad-string, url,
bad-url, number, percentage, dimension, whitespace, comment, CDO, CDC, colon,
semicolon, comma, `(`, `)`, `[`, `]`, `{`, `}`, delimiter, EOF and invalid — with
`type_is_known`, `type_is_block_start`, `type_matching_close` and, for numbers,
the numeric value, the integer and sign flags, the raw representation and the
unit.

**Phase 9.1 defines the record; 9.2 fills it.** The split between them is the
point: `text` is left as the **raw bytes** so it can never disagree with `range`,
and the token's resolved *value* (escapes applied, delimiters stripped) is reached
on demand through the `robo_css_token_value_*` accessors, which work from `text`
alone. A tokenizer therefore never hands out a token pointing into scratch
storage, no token is invalidated by asking for the next one, and a token survives
being copied and kept as long as its source does.

### Tokenizer

`robo_css_tokenizer_t` (Phase 9.2) is the CSS Syntax state machine. It is a
by-value cursor the caller owns — `init`, `dispose`, `reset` — with no allocation
anywhere in the subsystem, and it consumes a `robo_css_source_t` without ever
rewriting the caller's bytes. It pairs with the token model rather than replacing
it: `robo_css_tokenizer_next` produces a token, and the token is an ordinary 9.1
record.

Its own architecture, the three decisions it turns on, the token categories,
source-range behaviour, the deterministic malformed-input outcomes and the stream
API are documented in `docs/Tokenizer.md`. Two properties matter to a reader of
*this* document: it holds no reference to a DOM node, a stylesheet or a parser,
and it records its first error with the same rule the parser cursor uses.

### Component value

`robo_css_value_t` is a component value: one token, or a `function(name …)`, or a
`block(( … ))`, or a `list(… …)`. Kinds are `NONE`/`TOKEN`/`FUNCTION`/`BLOCK`/
`LIST`, and only the last three hold children (`kind_holds_children`).

Children are **moved in, never copied in**. `robo_css_value_append` grows the
child array *first* and only then transfers the child, so a failed append leaves
the caller's value exactly as it was — still owning everything it owned before.
After a successful append the child is `NONE`, which is how a caller can tell the
move happened without a second return value. `robo_css_value_move` hands a whole
tree across an ownership boundary with no copy and disposes whatever the
destination held.

Depth is bounded by construction — the parser owns the nesting budget — so
`dispose`, `depth` and `tree_count` recurse over what could be *built*, not over
input.

### Property

A property's identity is **per table**. `robo_css_property_table_t` maps a
lowercased ASCII name to a stable id that is never reissued, and every id is
meaningful only against the table that issued it. Two tables are two worlds; this
is stated as a precondition rather than given a global registry, because a global
property registry is precisely the hidden global state the plan warns about.

The table also carries the two hooks later phases need — a property's *value
parser* and its *computer* — registered by id. 9.1 registers the mechanism and no
properties.

### Declaration

`robo_css_declaration_t` is `name`, `value`, `important`, `name_range`,
`value_range`, `range` and a state: `NONE` (never made), `PARSED` (a parse attempt
produced it) or `INVALID` (a parse attempt rejected it, and the record says so
rather than quietly being absent). `is_usable` and `has_value` are the two
questions a consumer asks.

`ROBO_CSS_ORIGIN_INLINE` is *not* a stylesheet origin, but declarations do carry
an origin, because inline declarations have to be attributable to something. See
§6.

### Selector

`robo_css_simple_selector_t` (kind, name, attribute, comparison value, match
operator, ASCII-case flag, `value_was_string`, pseudo argument) →
`robo_css_compound_selector_t` (an AND of simple selectors) →
`robo_css_complex_selector_t` (compound selectors joined by combinators) →
`robo_css_selector_list_t` (a comma list). `is_well_formed` checks the structure
each level owns.

`simple_selector_set_attribute` refuses a comparison with an *absent* value while
accepting a *present and empty* one. `[a]` and `[a=""]` are different selectors
and the model keeps them distinguishable; this is the source model's
present/empty distinction reappearing one level up, on purpose.

### Rule

`robo_css_style_rule_t` holds a selector list plus a declaration list.
`robo_css_at_rule_t` holds a name, a prelude, a block and nested rules.
`robo_css_rule_t` is a tagged union of the two plus `kind`, `range` and any
metadata, and `robo_css_rule_list_t` flattens nested rules in document order.

### Stylesheet

```text
StyleSheet                       a slot: id, origin, state, label, source, rules
  └── RuleList
        ├── StyleRule
        │     ├── SelectorList
        │     └── DeclarationList
        └── AtRule
              ├── prelude
              └── RuleList (nested)
```

States are `CREATED` (exists, no parse attempt), `PARSED` (an attempt finished and
produced this rule list — a *recovered* parse is still `PARSED`) and `FAILED` (the
attempt could not produce a usable stylesheet at all). There is no `DESTROYED`
state: a destroyed stylesheet is an id that no longer resolves.

`robo_css_stylesheet_parse` is the declared boundary for 9.3. It returns
`ROBO_NOT_SUPPORTED`, records an `UNSUPPORTED` reason both in the caller's record
(if the caller wanted one) and on the stylesheet, and **touches nothing else** —
rules, state and source are exactly as they were.

## 6. The DOM boundary

RoboCSS must eventually answer:

```text
DOM Element  →  style resolution  →  computed style
```

9.1 defines the seam and implements none of the resolution:

* `robo_css_element_ref_t` — an opaque host-supplied element identity. It is a
  pointer or an index as far as RoboCSS is concerned; RoboCSS never dereferences
  it and never assumes it is a node handle.
* `robo_css_element_view_t` — what a *matcher* is given: tag name, id, class
  slices, attribute name/value pairs, parent and previous-sibling references, and
  a borrowed opaque userdata.
* `robo_css_element_lookup_fn` — a host callback that turns an element ref into a
  view, in the host's own terms.
* The **style context** (`RoboCSSStyle.h`) holds the property table, the
  stylesheets in scope with their origins, and the two borrowed callbacks, and
  exposes `robo_css_style_context_match_declarations` /
  `robo_css_style_context_compute_style`, both of which answer
  `ROBO_NOT_SUPPORTED`.

The DOM stays authoritative for elements, attributes, tree relationships and text
nodes. RoboCSS is authoritative for CSS source, stylesheets, rules, declarations
and — later — resolution. **RoboCSS does not duplicate the DOM tree**, holds no
node registry, and caches no view across a call.

The host constructs the view, which means the boundary can be driven today
exactly the way 9.4 and 9.8 will drive it, and `css.style.context_borrows` does
exactly that: it asserts the contract a host has to satisfy.

## 7. The inline-style boundary

```html
<div style="color: red">
```

The DOM keeps the `style` attribute. RoboCSS parses its *text* into declarations
on request through `robo_css_declaration_list_parse_text`, which answers
`ROBO_NOT_SUPPORTED` in this phase while already enforcing the shape of the seam:

* declarations produced carry `ROBO_CSS_ORIGIN_INLINE`;
* an empty or absent attribute is **no declarations** (not an error, not an empty
  declaration);
* the cleartext is borrowed from the caller and never copied into the model;
* the DOM view RoboCSS is handed is read-only.

There is no second attribute store and no shadow declaration list. If a caller
wants the inline declarations, it asks for them each time — which is correct,
because the DOM may have changed since the last time it asked.

## 8. The default-stylesheet boundary

`robo_css_default_stylesheet_text` is the runtime-provided (user-agent) origin's
entry point, returning `NULL` with length `0` in this build. That is the honest
representation of "this runtime ships no default styles yet", and a caller must
read it as *no runtime styles* rather than as failure.

`ROBO_CSS_ORIGIN_RUNTIME` exists as an origin, `robo_css_origin_is_stylesheet_origin`
accepts it, and no default stylesheet text is embedded. The plan's "do not add a
large built-in stylesheet now" is honoured literally: there is a boundary, and
behind it there is nothing.

## 9. Origin and the cascade

Origin in 9.1 is **provenance, not priority**:

```text
ROBO_CSS_ORIGIN_NONE / RUNTIME / USER / AUTHOR / INLINE
```

A stylesheet may only carry `RUNTIME`, `USER` or `AUTHOR`
(`robo_css_origin_is_stylesheet_origin`); `INLINE` is a declarations-level origin
and is explicitly *not* a stylesheet origin, because an element's `style`
attribute is not a stylesheet and never becomes one here.

No priority is assigned. The cascade, specificity and inheritance are 9.5, 9.6
and 9.7, and none of them is implemented or approximated.

## 10. Errors

`robo_css_error_t` is a *value*, never control flow:

| Field | Meaning |
|---|---|
| `kind` | The CSS problem: `NONE`, `UNSUPPORTED`, `RECOVERABLE_SYNTAX`, `INVALID_TOKEN`, `MALFORMED_DECLARATION`, `MALFORMED_RULE`, `UNEXPECTED_EOF`, `OUT_OF_MEMORY` |
| `code` | The matching `robo_result_t`, so a caller keeps switching on the existing vocabulary |
| `position` | Byte offset, and line/column once the source was consulted |
| `message` | A static or caller-lifetime string, never freed by RoboCSS |

`recoverable` (a parser can continue and still produce a stylesheet) is
distinguished from `fatal` (`UNSUPPORTED`, `OUT_OF_MEMORY`). Fatal means "this
attempt is over" — **never** "the process dies". No RoboCSS path terminates the
caller and no entry point fails the process on malformed CSS; malformed input is
carried, described and released.

The parser's cursor keeps the **first** error and does not let a later one
overwrite it, because after a recovery path has skipped three constructs the
useful report is still the first problem. `robo_css_parser_take_error` hands the
record out and clears it.

## 11. Memory and lifetime

* One growable-array rule (`robo_css_grow`) serves every collection. The
  collections are deliberately *not* a shared generic container type: they hold
  owning elements and are appended by moving into them, and a generic container
  would invite a caller to copy one in.
* The module owns a static, bounded slot table. `robo_css_shutdown` releases every
  live stylesheet, so a suite can assert an exact live count either side of a
  scope.
* Every suite that allocates measures `robo_mem_debug_live_count()` around its
  scope and asserts it returns to the baseline. `robo_css_shutdown` is also
  asserted to release what it handed out.
* Sanitizers are clean: AddressSanitizer and UndefinedBehaviorSanitizer both run
  over the whole suite with zero findings. See `docs/Tests.md`.

## 12. What the phases so far explicitly do not do

Parsing (9.3), selectors and matching (9.4), the cascade (9.5), specificity
(9.6), inheritance (9.7), computed values (9.8), colours (9.9), units (9.10), the
box model (9.11), display (9.12), positioning (9.13), backgrounds (9.14), borders
(9.15), conformance tests (9.16), layout, painting, rendering, CSSOM, animations,
transitions, external stylesheet fetching and networking. **Reading bytes is not
interpreting them**: the tokenizer knows that `10px` is a dimension with the unit
`px`, and deliberately knows nothing about what a length means.

Each one exists here as a *declared seam* that answers `ROBO_NOT_SUPPORTED`, and
`robo_css_get_capabilities` reports that it is absent — the capability flag for
the tokenizer flipped to 1 in 9.2 for exactly that reason, and no other flag did.
