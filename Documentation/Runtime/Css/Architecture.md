# RoboCSS Architecture (Phase 9.1–9.16)

This is the design record for RoboCSS as it exists after Goal 9.9. It documents
boundaries and rules, not plans: everything described here is compilable code
with tests behind it, and everything *not* described here is a boundary that
answers `RoboNotSupported`. Where a phase can see that a question exists and
cannot answer it, it *reports* that rather than guessing — an unevaluatable
selector is `unsupported` and a relation the host cannot answer is *undecided*,
and both are documented where they occur (`docs/Cascade.md`, `docs/Selectors.md`).

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
CssValue v = RoboCssCleared;
CssValueMakeList(&v, NULL);          /* v is a LIST */
CssValueAppend(&v, &child, NULL);     /* v owns one child */
CssValueMakeFunction(&v, name, r, NULL);
                                             /* the child array is released here,
                                              * not leaked */
```

The price is a precondition, stated once in `RoboCSSTypes.h` and asserted by
`css.records.cleared_state`: **a record must be cleared before its first use.**
`RoboCssCleared` is the all-zero state, and every field's zero is its empty
value — each `*_NONE` enumerator is 0, an absent slice is `NULL/0`, an empty range
is `0/0`, an empty collection is `NULL/0/0`. So

```c
CssValue a = RoboCssCleared;
CssValue b;
CssValueInit(&b);
```

produce the same record. Zeroing by any other route (memset, a caller's own
initializer) is equally valid. Only leaving a record uninitialized is not.

Why this is not merely a documentation preference: if a record reaches a release
call uninitialized, the release step walks a `count` that holds stack garbage and
frees a pointer that was never an allocation. This was observed during 9.1 and is
exactly the class of defect the rule exists to make impossible to write by
accident.

## 4. The source model

`CssSource` answers "what bytes did we get, and where are we in them".

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

`CssToken` is the full CSS Syntax vocabulary as an enum — identifier,
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

`CssTokenizer` (Phase 9.2) is the CSS Syntax state machine. It is a
by-value cursor the caller owns — `init`, `dispose`, `reset` — with no allocation
anywhere in the subsystem, and it consumes a `CssSource` without ever
rewriting the caller's bytes. It pairs with the token model rather than replacing
it: `CssTokenizerNext` produces a token, and the token is an ordinary 9.1
record.

Its own architecture, the three decisions it turns on, the token categories,
source-range behaviour, the deterministic malformed-input outcomes and the stream
API are documented in `docs/Tokenizer.md`. Two properties matter to a reader of
*this* document: it holds no reference to a DOM node, a stylesheet or a parser,
and it records its first error with the same rule the parser cursor uses.

### Grammar (the parser)

`CssGrammar` (Phase 9.3, `RoboCSSParserGrammar.c`) is the CSS Syntax
*structure* rules: a stylesheet, a rule list, an at-rule, a qualified rule, a
declaration block, a declaration, and component values. It holds a tokenizer **by
value** and is itself a stack record, so the only parsing-time allocation is a
second stack tokenizer for one speculative pass (see below).

It reaches the caller through two doors into that one grammar:
`CssStylesheetParse` (a sheet's attached source → its rule list) and
`CssDeclarationListParseText` (a caller's bytes → a declaration list).
Both call the same `grammar_consume_declaration_block`, which is what makes a
`style` attribute and the inside of a `{ … }` incapable of drifting apart.

Three properties are architectural rather than incidental:

* **The grammar lexes nothing.** Every byte arrives as a 9.2 token and every token
  it keeps is that same record — same type, same range, same hash classification,
  same dimension unit, same numeric payload. It builds and interprets nothing:
  a prelude is component values (so a style rule's selector list stays empty for
  9.4), an at-rule is syntax with no table to look its name up in, and
  `!important` is recorded without ordering anything.
* **It is transactional and separates two kinds of outcome.** The parse builds
  into its own rule list and only a parse that returned `RoboOk` replaces the
  sheet's rules, so parsing twice is idempotent rather than additive. The *return
  value* reports the call; `out_error` reports the CSS's first problem and is
  always filled. A recovered parse is still `PARSED`.
* **Nesting is bounded, and past the bound a construct is flattened rather than
  abandoned.** `RoboCssMaxNesting` (64) caps both recursions. The over-deep
  construct's tokens are still consumed, the rule is still produced, and only its
  contents are dropped — so a hostile sheet costs bounded stack and, crucially,
  cannot cost the rules that follow it.

One design point is worth stating here because it is the only place the grammar
speculates. A block's contents are rules for some at-rules and declarations for
others, and an at-rule the build has never heard of could be either — so the block
is read as a rule list first, and when that yields nothing the same bytes are read
again as declarations, on a **tokenizer of its own over the same source** so no
range has to be adjusted afterwards. The guess can be wrong in the direction of
complaining about legal input (`@font-face { font-family: X }`), so the grammar
saves its error state around the guess and restores it when the second reading
succeeds.

The full architecture — the stage map, the declaration and `!important` rules,
the at-rule treatment, the two recovery boundaries, the range conventions and the
lifetime rules — is in `docs/Parser.md`.

### Component value

`CssValue` is a component value: one token, or a `function(name …)`, or a
`block(( … ))`, or a `list(… …)`. Kinds are `NONE`/`TOKEN`/`FUNCTION`/`BLOCK`/
`LIST`, and only the last three hold children (`kind_holds_children`).

Children are **moved in, never copied in**. `CssValueAppend` grows the
child array *first* and only then transfers the child, so a failed append leaves
the caller's value exactly as it was — still owning everything it owned before.
After a successful append the child is `NONE`, which is how a caller can tell the
move happened without a second return value. `CssValueMove` hands a whole
tree across an ownership boundary with no copy and disposes whatever the
destination held.

Depth is bounded by construction — the parser owns the nesting budget — so
`dispose`, `depth` and `tree_count` recurse over what could be *built*, not over
input.

### Property

A property's identity is **per table**. `CssPropertyTable` maps a
lowercased ASCII name to a stable id that is never reissued, and every id is
meaningful only against the table that issued it. Two tables are two worlds; this
is stated as a precondition rather than given a global registry, because a global
property registry is precisely the hidden global state the plan warns about.

The table also carries the two hooks later phases need — a property's *value
parser* and its *computer* — registered by id. 9.1 registers the mechanism and no
properties.

### Declaration

`CssDeclaration` is `name`, `value`, `important`, `name_range`,
`value_range`, `range` and a state: `NONE` (never made), `PARSED` (a parse attempt
produced it) or `INVALID` (a parse attempt rejected it, and the record says so
rather than quietly being absent). `is_usable` and `has_value` are the two
questions a consumer asks.

`RoboCssOriginInline` is *not* a stylesheet origin, but declarations do carry
an origin, because inline declarations have to be attributable to something. See
§6.

### Selector

`CssSimpleSelector` (kind, name, attribute, comparison value, match
operator, ASCII-case flag, `value_was_string`, pseudo argument) →
`CssCompoundSelector` (an AND of simple selectors) →
`CssComplexSelector` (compound selectors joined by combinators) →
`CssSelectorList` (a comma list). `is_well_formed` checks the structure
each level owns.

`simple_selector_set_attribute` refuses a comparison with an *absent* value while
accepting a *present and empty* one. `[a]` and `[a=""]` are different selectors
and the model keeps them distinguishable; this is the source model's
present/empty distinction reappearing one level up, on purpose.

Three fields exist because 9.4 needed them and neither earlier phase could have
invented them correctly:

| Field | Why |
|---|---|
| `CssSelectorList::names` | An owned arena holding the resolved spelling of every name in the list, `:not()` arguments included. Escapes are resolved **once**, here, so a matcher compares the bytes CSS meant rather than the bytes the author typed. The parser reserves it up front from the prelude's length, so interning can never reallocate a name it has already handed out |
| `CssSimpleSelector::nested_index` | An index into the owning compound's nested argument lists, not a pointer. A simple selector therefore owns nothing, which is what makes `compound_selector_append` a safe by-value copy — and the copy clears the index, because an index names a list inside the compound that issued it |
| `nth_parsed` / `nth_a` / `nth_b` | `:nth-child()`'s An+B, parsed once at parse time. `a` is signed, which is what makes `-n+3` and `n-1` expressible; `a == 0` is the bare-integer form |

A `:not()` argument is a full `CssSelectorList` owned by the compound,
which is the one place in the model where a list contains a list. It disposes
recursively, and nesting is capped at `RoboCssMaxSelectorNesting` (8) — past
the cap the selector is **rejected**, because depth costs stack and the budget is
there to bound it rather than to be exceeded politely.

The selector grammar over these records is `RoboCSSSelectorParser.c` and the
matcher is `RoboCSSMatcher.c`. `docs/Selectors.md` is their document; this file
records only why the *model* is shaped the way it is.

### Specificity

Specificity is **not** a fourth record beside these. It is three counts *about* a
selector, and the one thing it must never be is a second description of one — so
`RoboCSSSpecificity.h` computes them by walking the same simple/compound/complex
records the matcher walks, and there is no field on a selector record that could
disagree with a calculation. Nothing is stored: a specificity is derived on
demand from immutable records, so it cannot go stale and no cache can be wrong.

The representation is a tuple of three counts, not a packed integer:

```text
(A, B, C)
  A = #id
  B = .class + [attribute] + :pseudo-class
  C = type + ::pseudo-element
```

Each count is clamped at `RoboCssSpecificityComponentMax`, so the tuple is a total order
ever selector depth and saturation is a stated behaviour rather than a wrap. A
packed integer would have the same comparison outcome only until a component
overflowed into its neighbour, at which point a more specific selector would
compare as less specific — an ambiguity the tuple cannot represent. A selector's
specificity, a whole selector list's per-member specificities, and the "most
specific member" a rule's list is worth to the cascade are all the same three
numbers.

### Property registry

The property *table* (`RoboCSSProperty.h`) is identity, per caller. The property
*registry* (`RoboCSSPropertyRegistry.h`) is CSS's own metadata — does a property
inherit by default, and what is its initial value — and it is deliberately the
other object rather than more fields on the table: it issues no ids, resolves no
names to identities and holds no per-caller state. A definition's initial value is
stored as text **and** as the component-value tree that text parses to, built once
at registration through the same grammar a declaration goes through, so a
resolution never reparses an initial value per element and an initial value cannot
be a different dialect of CSS. The two objects meet through
`CssPropertyRegistryApplyToTable`, which sets
`RoboCssPropertyFlagInherited` from the registry's classification, so the flag
a caller's table carries and the classification cannot drift.

### Inheritance

Inheritance is **not** a record in the source sense either. It is a *result*:

```text
InheritedStyle                   what one element resolved to
  ├── entries                    borrowed-free: one owned name + one owned value each
  │     ├── property             identity from the context's table, or NONE
  │     ├── name                 copied (the entry does not borrow the sheet)
  │     ├── value                deep-copied (the entry does not borrow the parent)
  │     ├── source               DECLARED / PARENT / DEFAULT / DEFERRED
  │     └── keyword              the CSS-wide keyword that produced it, or NONE
  └── counters                   declared / inherited / default / deferred / unresolved
```

It is produced by `RoboCssInheritResolve`, which takes the parent as an
**argument** and the cascade as an argument, so a resolution is a pure function of
its inputs: nothing is cached between calls and no global "current style" exists.
The context door (`CssStyleContextResolveInheritedStyle`) is the only
thing that walks a tree, and it does so by asking the same host the matcher asks
about one element's parent — never RoboHTML. The registry is borrowed by the style
context exactly as the property table is, because which properties exist and how
they are classified is *policy* and policy belongs to the caller.

### Computed value

A *computed* value is the typed answer for one property, and it is a closed set
of shapes rather than "a string, but typed":

```text
ComputedValue                    what one property's value is
  ├── kind                       KEYWORD / NUMBER / PERCENTAGE / LENGTH / COLOR /
  │                              FAMILY_LIST / PRESERVED / INVALID
  ├── number, unit, is_integer   NUMBER, PERCENTAGE and LENGTH
  ├── color                      four 8-bit channels
  ├── text                       the keyword, the family list, or the reason
  ├── preserved                  the specified tokens, owned (a deep copy)
  └── unresolved                 a reference only a later stage can settle
```

It is produced by `CssComputedValueCompute`, which is a function of **the
value's shape and the value** — the shape coming from the registry's `computation`
column — and never of a property *name*. That is what keeps it context-free:
no containing block, no font metrics, no parent, no element. `PRESERVED` is a real
computed value (a `calc()`, an `env()` reference, a custom property's opaque token
sequence), `INVALID` is an *answer* the call reports successfully rather than an
error, and `unresolved` is a flag no kind can express. The container it lands in is
`CssComputedStyle` — the 9.1 container, unchanged in shape, with each
entry now also carrying its name, its typed value and a provenance bitset.
`docs/ComputedValues.md` has the full account.

### Custom-property substitution (18.1)

The one part of this model that is a *pass over a finished computed style* rather
than a per-property normaliser is the substitution of `var()` references
(`RoboCSSVariable.h`). A custom property was already first-class here — an opaque
token sequence, ordered by the cascade and copied down by inheritance — and a
reference was recognised and carried; 18.1 replaces it, after the per-property
pass and before `currentColor` is settled, because a reference can *produce* a
`currentColor`.

It is a pure function of (the value, a computed style carrying the custom
properties in scope): no DOM, no font, no box, no parent. A reference that cannot
be substituted — undefined with no fallback, or a cycle, or a chain deeper than
`RoboCssVariableMaxDepth` — is answered as CSS's *invalid at computed-value time*,
so the declaration takes no value and the property follows `unset` (the parent's
computed value if it inherits, its own initial value otherwise) rather than being
marked `INVALID`, because the declaration was legal and the property still has a
correct value. `docs/Variables.md` is the authority, including the one pass-order
boundary this phase names: a shorthand whose component count a substitution would
change is expanded before this pass runs and takes `unset` rather than being
distributed.

One model surface is added by it — `RoboCssComputedFlag` is unchanged, but
`CssCapabilities` gains `customProperties` and `variableSubstitution` — and the
module moves to `0.19.0`.

### The object model (18.2)

The one part of this architecture that *produces* structure rather than reading
it is the object model (`RoboCSSOM.h`). Two halves, and neither existed before:
`cssText` — a canonical, re-parseable rendering of a component value, a selector
list, a declaration, a rule, a rule list or a whole stylesheet — and
`CssStyleDeclaration`, a self-contained mutable set of properties that can be
parsed, read, edited, removed and rendered (`element.style`'s shape).

The rendering is deliberately *not* the specification's byte-exact serialization
(no number canonicalisation, no colour normalisation); it is a spelling of the
model that the parser reads back to the same model, which is the property the
round-trip test asserts. The declaration is a *second* container rather than a
mutable view of `CssDeclarationList` because a parsed declaration borrows from
its source while `setProperty` must work after that source is gone — so each
property copies its name and owns its value's bytes. A stylesheet's top-level
rule list is also how `insertRule`/`deleteRule` edit a live sheet. The CSSOM's
document binding — `styleSheets`, `getComputedStyle`, a parent-stylesheet chain,
a live style context — is deliberately absent and named in `docs/Cssom.md` §4,
because it needs a DOM. One capability joins the table (`cssom`), one diagnostic
domain (`RoboCssDomainCssom`), and the module moves to `0.20.0`.

### The flexbox vocabulary (18.3)

The flexbox family (`RoboCSSFlex.h`) is the first family in this architecture whose
values are **three** vocabularies rather than one, and it is the family a layout
phase will read to place an item's box:

```text
FlexDirection     row | row-reverse | column | column-reverse
  ├── row               the main axis is the inline axis
  └── reversed          the start and end edges are swapped
FlexWrap          nowrap | wrap | wrap-reverse
  ├── wrapped           the items break into several lines
  └── reversed          the cross-axis line order is swapped
FlexAlign         one enumerator set, four properties
  ├── justify           `justify-content`
  ├── items             `align-items`
  ├── self              `align-self`   (also `auto`)
  └── content           `align-content`
```

The alignment half is the part worth reading, because it is the first vocabulary
here shared by more than one property in a way that is not a shorthand and its
longhands. CSS Box Alignment defines `justify-content`, `align-items`, `align-self`
and `align-content` as *subsets of one set of values*, and the subsets are not
nested: `space-between` is legal for the two container properties and not for the
two item properties, `baseline` for the three alignment properties and not for
`justify-content`, and `left`/`right` for `justify-content` alone. So the module has
**one** enumerator set and **four** value shapes, and which values each property may
hold is a *column of the vocabulary's rows* rather than a branch at a call site.
That is the same move `display` made in 9.12 — a closed set whose classifications a
consumer dispatches on — made once for four properties instead of one.

The four remaining facts are the properties the family adds around the alignment
set: `flex-grow`/`flex-shrink` (one `FLEX_FACTOR` shape — a `<number>` that must not
be negative, with no upper bound, so a negative one is a refusal rather than a
clamp), `flex-basis` (a length family in the shared grammar: a length, a percentage,
or `auto`/`content`/`min-content`/`max-content`), `order` (an `ORDER` shape — a
signed `<integer>`, which is the property's whole point) and the two shorthands
`flex-flow` and `flex`, which are **carried declarations** rather than vocabularies
because each stands for two halves of different kinds. Nothing in this build expands
a shorthand, so `flex-flow: column wrap` reaches a consumer as the tokens the author
wrote and each half is read from its own longhand. `docs/Flexbox.md` is the
authority, including that boundary and the layout this phase does *not* do: no line
is measured, no free space is distributed, and no main-axis position is computed.

One capability joins the table (`flexbox`), one diagnostic domain
(`RoboCssDomainFlex`), and the module moves to `0.21.0`.

### The grid vocabulary (18.4)

The grid family (`RoboCSSGrid.h`) is deliberately half a family, and the split is
the interesting part:

```text
grid-auto-flow        row | column | row dense | column dense
  ├── column          auto-placement follows columns
  └── dense           holes are backfilled
row-gap / column-gap  normal | <length-percentage>  (the shared length grammar)
gap, grid, grid-template, grid-row, grid-column, grid-area ...  carried
```

`grid-auto-flow` is the first closed vocabulary in this build whose values are not
all one identifier: CSS writes it as `[ row | column ] || dense`, so `row dense`,
`dense row` and `dense` are one value. A single-identifier table would either lose
the density — the one thing density changes, since a dense container may place a
later item *before* an earlier one — or invent a fifth row for `dense`. So the module
reads the **component value** (`CssGridAutoFlowFromValue`), the way
`background-position` reads its pair, classifies each component as an axis or a
density, refuses a repeated half, and reports the value as the two facts a layout
pass dispatches on (`IsColumn`, `IsDense`) — which is what the vocabulary's rows
carry as columns.

The gutters are not a shape of their own: a gutter *is* a length, so `row-gap` and
`column-gap` join the one length grammar `flex-basis` and the box edges already
share, with `normal` as their own keyword — and `normal` is *not* zero, so it is
carried as the keyword it is rather than resolved to `0px`. Everything else in the
family takes a **track list** (a size, a `minmax()`, a repeat count, a line name) or
a **line placement** (a number, a `span`, a custom ident), and those are a phase of
their own: the track templates, the placements and the shorthands `grid`,
`grid-template`, `grid-row`, `grid-column`, `grid-area` and `gap` are registered as
**carried declarations** with their real initial values, so the cascade and the
computed style stay correct until the grammar lands. As in 18.3, no shorthand is
expanded, and `docs/Grid.md` is the authority — including that no track is measured,
no item is placed and no line number is resolved.

One capability joins the table (`grid`), one diagnostic domain
(`RoboCssDomainGrid`), and the module moves to `0.22.0`.

### The table vocabulary (18.5)

The table family (`RoboCSSTable.h`) is the first in this architecture whose members
are **mostly inherited** — and that, rather than any one value, is what the module is
for. CSS gives a table box five properties that describe it before a column is
measured, and a table's *cells* take four of them from the box: the collapse, the
spacing, the caption side and the empty-cell rule propagate, while exactly one —
`table-layout` — is the box's own.

```text
table-layout      auto | fixed
  └── content-driven   auto
border-collapse   separate | collapse
  └── borders adjoin   collapse
caption-side      top | bottom | block-start | block-end | inline-start | inline-end
  └── block / inline axis  the two logical pairs, reported separately
empty-cells       show | hide
  └── draws empty      show
visibility        visible | hidden | collapse
  └── takes space      visible, hidden   (collapse does not)
  └── paints           visible
```

Five closed sets, and none of them needs a computed-value kind of its own: each
value *is* its keyword, and a typed enumerator with no reader yet would be a second
copy of a table for the reader table layout (19.6) will bring. So all five are rows
in the shared closed-vocabulary body the display and position shapes use — the one
whose make door carries the keyword — and what the closed set buys is the refusal:
`table-layout: banana` is a mistake, not a keyword carrying `banana`.

`caption-side` is the first vocabulary here whose members are *logical*:
`block-start` names the block-start edge of whatever writing mode is in force, so
the module reports the axis and the phase that owns a writing mode resolves the
physical edge. The physical rows are neither axis, which is why `IsBlockAxis` and
`IsInlineAxis` are two questions rather than one negation.

`border-spacing` is deliberately **not** a vocabulary. Its grammar is
`<length>{1,2}` — the shared length grammar with a pair above it — so it is carried
PRESERVED until the phase that reads it, exactly as the grid track templates are.
And the internal table display types (`display: table`, `table-row`, `table-cell`,
…) are **not** rows in this build's display vocabulary: an internal table box is a
layout fact of its own, and the phase that introduces it is table layout (19.6).
This phase types the properties a table box is described by and names the boxes it
does not yet have rather than implying them.

One capability joins the table (`tables`), one diagnostic domain
(`RoboCssDomainTable`), and the module moves to `0.23.0`.

### The multi-column vocabulary (18.6)

The multi-column family (`RoboCSSColumn.h`) describes a *fragmentation* container,
and the fact worth reading is how little of it is new. Two properties are closed sets
of their own:

```text
column-span   none | all
  └── spans all   all
column-fill   auto | balance
  └── balances    balance
```

and the rest are shapes that already exist. `column-width` is the shared length
grammar with the keyword `auto`; `column-rule-width`, `column-rule-style` and
`column-rule-color` are the *border family's* line width, border style and colour
shapes, so a column rule is not a second border family; `column-gap` was already a
row from 18.4, because it is the same gutter `row-gap` is. Only `column-count` adds
arithmetic, and its bound (at least one) is why it is a shape of its own — zero
columns is not a smaller count, it is not a count. `column-fill`'s initial value is
`balance`, which is not the zero of its enumerator; the shape carries the keyword, so
the registry writes `balance` down and a cleared record cannot hold the wrong answer.

The two shorthands (`column-rule`, `columns`) are carried declarations like every
other shorthand here. Nothing fragments: no content is split, no height is balanced
and no column count is derived from a width — that is the phase with a box.

One capability joins the table (`multiColumn`), one diagnostic domain
(`RoboCssDomainColumn`), and the module moves to `0.24.0`.

### The writing-mode vocabulary (18.13)

The writing-mode family (`RoboCSSWriting.h`) is the three closed sets that together
decide which physical edge is inline-start and how a character stands in a vertical
line:

```text
direction         ltr | rtl
  └── right-to-left   rtl
writing-mode      horizontal-tb | vertical-rl | vertical-lr | sideways-rl | sideways-lr
  └── horizontal      horizontal-tb
  └── vertical        the four non-horizontal values
  └── sideways        sideways-rl, sideways-lr
text-orientation  mixed | upright | sideways
  └── sideways        sideways
```

The decision worth reading is that `writing-mode`'s four vertical values are **not**
a vertical/horizontal pair. `vertical-rl` and `vertical-lr` are vertical and keep the
characters mixed, while `sideways-rl` and `sideways-lr` are vertical *and* turn the
text, so the row carries two classification columns and `IsVertical` is **not** the
negation of `IsSideways` — a build that derived one from the other would answer that
`vertical-rl` is sideways. `text-orientation` is carried and inert in
`horizontal-tb`, exactly as `background-attachment: fixed` is a value with no
scrollport. All three properties are inherited, because a child on axes its parent
was not on cannot be described otherwise.

`direction` is the one **upgraded in place** rather than added: it has had the bare
KEYWORD shape since 9.7, so `direction: banana` used to compute to a keyword carrying
the letters. Its `computation` becomes the closed set, its name, inheritance and `ltr`
initial are unchanged, and the conformance row did not move — a closed vocabulary of
keywords still computes to a *keyword*, and the refusal is the only thing that
changed. This is the same "shape change, not a new property" rule the border family
recorded for `border-width` and 9.13 for `position`.

What the phase does not claim is vertical *layout* or a vertical *shaper*: no run,
line, caret or fragment is derived from a writing mode in RoboCSS, and the two
`sideways-*` values are carried as values the build's text engine does not yet draw.
The value says *which axis is which*, and the module that has a box reads it.

One capability joins the table (`writingModes`), one diagnostic domain
(`RoboCssDomainWriting`), and the module moves to `0.25.0`.

### The transform vocabulary (18.7)

The transform family (`RoboCSSTransform.h`) is two ordinary keyword sets and one
*function-name* set:

```text
transform-style       flat | preserve-3d
  └── 3D context        preserve-3d
backface-visibility   visible | hidden
  └── shows back face   visible
<transform-function>  matrix, matrix3d, translate, …, rotate3d, skew, perspective
  └── is 3D             a per-function column
  └── is matrix         matrix, matrix3d
```

The substance is the function-name set, because it is what a `transform` list is
checked against. `transform` itself is a grammar — an ordered list of functions with
arguments — so it is registered as a **carried declaration** like the grid track
templates and `border-spacing`'s pair, and what this phase supplies is the closed set
of names plus the two classifications a later pass reads: whether a function operates
in 3D (`Is3D`) and whether one member already fixes the whole transform (`IsMatrix`).
So `transform: frobnicate(1)` is a mistake rather than a carried token, while
`transform: translate(1px) rotate(10deg)` is carried until the phase that composes it.

Nothing composes a list, multiplies two matrices, resolves an origin or draws
a turned box; the value says what an author wrote and what kind of transform the box
is in. One capability joins the table (`transforms`), one diagnostic domain
(`RoboCssDomainTransform`), and the module moves to `0.26.0`.

### The transition vocabulary (18.8)

The transition family (`RoboCSSTransition.h`) is mostly *grammar*, so it is mostly
carried — a property-name list, a time list, a timing-function list and the shorthand
that concatenates all four. What the phase types is one closed set and one keyword
vocabulary:

```text
transition-behavior   normal | allow-discrete
  └── allows discrete   allow-discrete
timing-function keywords
  ease | linear | ease-in | ease-out | ease-in-out | step-start | step-end
  └── is discrete step  step-start, step-end
```

The timing-function vocabulary is deliberately the **keyword half** of the property's
grammar: the property also admits `cubic-bezier()`, `steps()` and `linear()`, and
those are carried rather than represented. So the set here is what a consumer reads to
classify an easing (discrete or continuous) — not a claim that the whole grammar is
parsed. Nothing starts a transition, samples a curve or interpolates a value.One capability joins the table (`transitions`), one diagnostic domain
(`RoboCssDomainTransition`), and the module moves to `0.27.0`.

### The animation vocabulary (18.9)

The animation family (`RoboCSSAnimation.h`) is four closed sets, one numeric shape and
the rest carried:

```text
animation-direction     normal | reverse | alternate | alternate-reverse
  └── reverses / alternates    two separate answers
animation-fill-mode     none | forwards | backwards | both
animation-play-state    running | paused
animation-composition   replace | add | accumulate
animation-iteration-count   infinite, or a non-negative number
```

`animation-direction` carries two classification columns because an alternating
animation reverses only on the odd iterations, so a consumer must ask both.
`animation-iteration-count` is a shape of its own because its bound is a *floor*: a
half iteration is meaningful, so a fraction is a value and only a negative count is a
mistake, while `infinite` arrives as the keyword it is. `animation-timing-function` is
not a second copy of the transition family's keyword set (§18.8) — it is that set
reused, the same reuse `column-rule-*` show for the border shapes. The remaining rows
are carried grammars, and a `@keyframes` at-rule is the source a name refers to rather
than a property. Nothing starts an animation, resolves a keyframe or advances a
timeline. One capability joins the table (`animations`), one diagnostic domain
(`RoboCssDomainAnimation`), and the module moves to `0.28.0`.

### Media queries (18.10)

Media queries (`RoboCSSMedia.h`) are the first Phase 18 goal that adds no registry
row: a media query is `@media`'s prelude, not a property. The module owns the
media-type vocabulary (`all` | `screen` | `print`), the media-feature vocabulary with
its kinds, a `CssMediaContext` a caller fills in, and the condition/query matcher. A
feature's **spelling carries its operator** — `min-width` is the feature `width` with a
floor — so the range forms are not rows of their own and `CssMediaFeatureFromKeyword`
answers both the feature and the operator.

The matcher's answer has **three** values: yes, no and *unknown*. The third is
load-bearing: a feature the context cannot answer is `Unknown`, never `No`, because a
sheet that could not be decided is a different sentence from one that did not match.
A negated query swaps yes and no and leaves unknown alone. The phase does not parse a
prelude or attach a condition to an at-rule — that is 18.21's wiring — and does not own
a viewport: the caller copies numbers into the context. `@container` is 18.11's and
reuses this condition model. One capability joins the table (`mediaQueries`), one
diagnostic domain (`RoboCssDomainMedia`), and the module moves to `0.29.0`.

### Container queries (18.11)

Container queries (`RoboCSSContainer.h`) are the second goal to reuse instead of
reinventing: a container query's condition list is the **media-query** module's own
`CssMediaCondition` (§18.10), evaluated against a `CssMediaContext` holding the
*container's* numbers. What the phase adds is the `container-type` closed set
(`normal` | `size` | `inline-size`) with its two classifications and one matcher,
`CssContainerQueryMatches`. A `container-type: normal` box answers UNKNOWN rather than
matching, because a box that never declared itself a container is not a match against
something. No `@container` grammar is parsed and no ancestor chain is walked — the
caller that has a box finds its query container and hands the size here. One capability
joins the table (`containerQueries`), one diagnostic domain (`RoboCssDomainContainer`),
and the module moves to `0.30.0`.

### Logical properties (18.12)

Logical properties (`RoboCSSLogical.h`) own the logical side/axis vocabulary
(`block-start` | `block-end` | `inline-start` | `inline-end`) and the one resolver that
turns a role into a physical `CssBoxSide` under a `CssWritingMode` and `CssDirection`
(§18.13). The block axis is the mode's; the inline axis is the mode's turned by
`direction`, so a vertical `rtl` runs its inline axis bottom-to-top while the block axis
is unchanged, and the two `sideways-*` modes resolve as their vertical counterparts
because sideways turns the *text*, not the axes. The box model's logical longhands and
shorthands — `margin-block-start`, `padding-inline-end`, `inset-block-start`,
`block-size`, `inline-size` and the four axis shorthands — join the table as the **same
shape** their physical counterparts are, because turning a role into the physical edge
it names is the resolver's job rather than a second value model. Nothing maps an
element's *computed* style to physical values — that is the phase with an element
(19.10). One capability joins the table (`logicalProperties`), one diagnostic domain
(`RoboCssDomainLogical`), and the module moves to `0.31.0`.

### The font family (18.14)

The font family (`RoboCSSFont.h`) is the first here whose values describe a
**request rather than a result**: `font-weight: 700` asks for a face, and which face
answers is the font database's business — RoboFont, and for a shell document the
resolved font request 12.17 built. Nothing in the module opens a font file, so
nothing in it can be wrong about which face exists.

The phase's one real decision is that the nineteen rows split by **grammar** rather
than by importance. Six shapes cover nine properties — the four `font-synthesis-*`
longhands take the same two values, so a second table would be the same rows under
a second name — and each of the six is a genuinely closed set:

```text
font-style              normal | italic | oblique
  └── slanted / oblique   two answers: an italic is a design, an oblique is a turn
font-kerning            auto | normal | none
  └── enables kerning     auto, normal        (auto is "the font decides")
font-optical-sizing     auto | none
font-synthesis-*        auto | none           (four longhands, one vocabulary)
font-variant-caps       seven values
  └── affects lowercase   the two `all-` rows
font-variant-position   normal | sub | super
  └── shifted, superscript   a pair, because the shift has a sign
```

and nine are registered as **carried declarations** whose closed sets live in the
header for a consumer to read — `font-stretch`'s nine keywords and their
percentages, `font-size`'s ten size keywords and their ratios,
`font-variant-numeric`'s five independent axes and `font-variant-east-asian`'s
three, over the tag list a `font-feature-settings` is. That is 18.7's split for
`transform` and its function-name set made again: a grammar wider than any closed
set is carried, and what is *inside* it is typed.

Two rows are worth reading twice. `font-style` is **upgraded in place** from the
bare KEYWORD shape it had since 9.7, exactly as 18.13 upgraded `direction` — its
inheritance and its `normal` initial are unchanged and only the reading of the
value became closed, so `font-style: banana` stops being a keyword carrying the
letters. And `font-size` does **not** move: the shared length grammar has carried
its ten size keywords since 9.10, so a second value model would be the same
normalisation twice; what the phase adds is the classification — `medium` is not
sixteen pixels, because the absolute keywords are measured against the reader's
preferred size and `larger`/`smaller` against the parent's.

The second half of the phase is `@font-face`, and it is why a *descriptor*
vocabulary exists rather than more properties. Inside that block the same spellings
mean something different — `font-weight: 700` declares the face's own weight rather
than selecting one — and eight of the twenty-seven names exist only there.
`font-display` is the one closed set here that is not a property at all, so nothing
in the cascade can set it; what it carries is **time** (whether a block period
applies, how long it is, and whether the fallback is replaced afterwards), which is
the one thing a consumer needs to decide what to draw on the first frame.
`font-variant-ligatures` is the row deliberately carried *without* a vocabulary:
its five tags and their policy are RoboText's (12.12), and a second copy here is
how the shape a CSS author writes and the policy a shaper reads would come to
disagree.

What the phase does not claim is a font database, a shaper, a measurer — and **any
synthesis**: `font-synthesis-*` is a permission an author writes, and this build
never emboldens or slants a face (12.3's rule). Nor a `font` shorthand distribution
over its seven longhands, nor the `@font-face` prelude grammar, nor
`font-style: oblique <angle>` as a value (refused, as it already was), nor
`font-stretch`'s percentage as a value (carried). One capability joins the table
(`fonts`), one diagnostic domain (`RoboCssDomainFont`), six computations join the
closed-vocabulary body, and the module moves to `0.32.0`.

### The text family (18.15)

The text family (`RoboCSSText.h`) is the family whose central fact is that **six
words hide four independent questions**. `white-space` is spelled
`normal | pre | nowrap | pre-wrap | pre-line | break-spaces`, and what those six
actually say is whether runs of spaces collapse, whether newlines are preserved,
whether a line wraps at all, and whether a *preserved* space may be broken after:

```text
                     collapses  preserves  wraps  breaks after a preserved space
normal                  yes        no        yes    no
pre                     no         yes       no     no
nowrap                  yes        no        no     no
pre-wrap                no         yes       yes    no
pre-line                yes        yes       yes    no
break-spaces            no         yes       yes    yes
```

Two pairs in that table are why the row carries four columns rather than one flag.
`normal` and `pre-line` agree on the first and third and differ only on whether a
newline survives, so "which one is this" is not a question about collapsing or
wrapping; `pre-wrap` and `break-spaces` agree on all three of the first columns and
differ only on the fourth, which CSS Text 4 added `break-spaces` for. A build that
stored the keyword and tested it would have to know both of those facts at every
call site.

The phase's second decision is that a classification is **never the negation of
another**. `text-align`'s values are two kinds of statement — `start`/`end` are
relative to the inline direction and `left`/`right` are physical, while `center` is
neither — so `IsLogical` and `IsPhysical` are both false for it, and a consumer
that read "not logical" as "physical" would map a `center` through a direction it
has nothing to do with. The same shape recurs in `text-decoration-style`, where
`double` is not "not solid" (it is two unbroken strokes, not a stroke with gaps),
and in `text-decoration-line`, where `blink` draws no ink of its own.

Twenty rows split by **grammar**: ten take a typed shape (`text-align`,
`text-align-last`, `text-justify`, `text-transform`, `white-space`, `word-break`,
`overflow-wrap`, `line-break`, `hyphens`, `text-decoration-style`), and ten are
carried declarations whose closed sets live in the header for a consumer to read —
`text-decoration-line` is a *set*, `text-overflow` may be a `<string>`,
`text-decoration-thickness` may be a length, `text-underline-position` is a set of
two different questions, and the `text-emphasis-*` marks have no renderer in this
build to draw them. That is 18.7's and 18.14's split made once more.

Three rows are **upgraded in place** — `text-align`, `text-transform` and
`white-space` move off the bare KEYWORD shape they had since 9.7, so
`white-space: banana` stops being a keyword carrying the letters while their
inheritance and initials are unchanged — and `text-indent` deliberately does not
move: its `hanging`/`each-line` keywords are named as not read rather than silently
dropped, and the property keeps the shared length shape it has had since 9.11.

Fourteen of the twenty rows are inherited and six are not, and the split is the
family's central fact stated as a count. An alignment, a transformation, a
white-space rule, a line-breaking control, a shadow and an emphasis mark describe
how the **text** is set, so a child inherits them; a decoration is painted per
element — a child of an underlined link is not underlined — so the four
`text-decoration-*` rows and `text-overflow` describe the box that draws or clips
the text and do not propagate. `text-emphasis-color` is the family's one inherited
*reference* initial (`currentcolor`), which means its default entry resolves only
where the element's own `color` is known — a fact about the cascade that shows up
as one more incomplete entry on an element whose `color` is itself invalid.

What the phase does not claim is a line breaker, a justifier, a hyphenator or a
text transformer: nothing here wraps a line, distributes slack, finds a hyphenation
point or changes a character's case, and `direction`, `writing-mode` and
`text-orientation` are 18.13's. A value in this family says what rule a text pass
follows; where the line breaks belongs to the phase that has a text engine (12.x).
One capability joins the table (`text`), one diagnostic domain
(`RoboCssDomainText`), ten computations join the closed-vocabulary body, and the
module moves to `0.33.0`.

### The image family (18.16)

The image family (`RoboCSSImage.h`) is the family whose central fact is that **an
`<image>` is not a file**. A `url("a.png")` names one, a `linear-gradient(...)`
computes one, an `image-set(...)` chooses between several and a `cross-fade(...)`
composes them; what they have in common is not a byte stream but a *description of
where pixels come from*, and the one fact the value itself carries is the name of the
function that describes it. So the family's largest table is the function vocabulary —
thirteen functions (`url`, `src`, `image`, `image-set`, `cross-fade`, `element`,
`paint` and the six gradient spellings) with the classifications a consumer switches
on: is this a gradient, is it repeating, which of the three axes does it run on, does
the value itself state an intrinsic size, and does it name a resource outside the
stylesheet.

Those classifications are columns rather than a tree. A repeating gradient is *also* a
gradient and is on an axis; a non-gradient is on **no** axis; and "does the value state
an intrinsic size" is a separate question from which kind it is — `image-set()`
resolves to one of its alternatives and inherits *that* image's size, while as a value
it states none. A build that read "not linear" as "radial" would sample a conic
gradient's stops by the wrong quantity, and a build that assumed every image had an
intrinsic size would size a gradient to zero.

The phase's second decision is that a classification is **never the negation of
another**. `image-rendering`'s `auto` is in neither the smoothing group nor the
pixelated group, so a consumer that read "not smoothing" as "pixelated" would pin the
choice `auto` deliberately leaves to the user agent; and `crisp-edges` and `pixelated`
are two different promises — "do not blur" against "show the source's pixels" — which
is why the table carries a fourth column for the pixel grid. The same shape recurs in
`object-fit`, where `none` preserves the aspect ratio **without** scaling, so
"preserves the ratio" is not "scales".

Five rows split by **grammar**. Two take a typed shape (`image-rendering`, `object-fit`)
and three are carried declarations whose closed sets live in the header for a consumer
to read: `image-orientation` is `from-image | none | <angle>` with an optional `flip`,
`image-resolution` is a `||` of two optional parts, and `object-position` is the
position *pair* the background module already owns rather than a second copy of that
grammar. `image-orientation: from-image` is the family's `from-font` — a fact about the
*file* rather than a policy of the user agent — which is why it is the initial and why
an unknown value answers its behaviour rather than discarding a rotated photo.

Three of the five rows are inherited and two are not, and the split is the family's
home stated as a count: how an image is *rendered*, how a file's own orientation is
*read* and its resolution are settings of the element that holds the image, so a child
inherits them; how a replaced element's content is *fitted to a box* is the box's own
business, so `object-fit` and `object-position` do not.

What the phase does not claim is a loader, a decoder, a renderer, a gradient sampler or
a layout pass: no function here opens a file, resolves a URL, samples a colour stop,
interpolates a gradient, scales a pixel or measures a box. A value in this family says
*which image* and *what rule* a renderer follows; the pixels belong to the phase that
draws them. One capability joins the table (`images`), one diagnostic domain
(`RoboCssDomainImage`), two computations join the closed-vocabulary body, and the
module moves to `0.34.0`.

### The background family completed (18.17)

Phase 18.17 *completes* the family 9.14 began rather than starting a new one, so no
diagnostic domain joins the table — it adds the values and properties `docs/Backgrounds.md`
§13 named as not built. The values are read the way the vocabulary was built to be read:
`background-repeat` gains `space` and `round` as rows 4 and 5, and they are rows rather
than a branch because they tile on both axes *like* `repeat` and differ only in how the
leftover is placed — distributed as gaps, or absorbed by stretching — so the row carries
two more columns and every accessor already reads them. `background-attachment` gains
`local` as its third value, and its predicate asks the one question that separates it: the
frame it moves with is the element's own content.

The properties are three, and two of them are one shape. `background-clip` and
`background-origin` share the **box** vocabulary — `border-box`, `padding-box`,
`content-box` — and differ only in which box is the initial, the one-vocabulary-two-
initials fact the display and positioning vocabularies record. Because the boxes nest, the
classifications are *inclusions* rather than a partition: `border-box` includes the border
and the padding, `padding-box` the padding and not the border. `background-blend-mode` is a
vocabulary of its own — sixteen modes in three groups: the identity `normal`, twelve
**separable** formulas that compute each channel independently, and four **non-separable**
ones (`hue`, `saturation`, `color`, `luminosity`) that mix the channels together. A
consumer with a per-channel fast path asks which kind it has first, and `normal` is in
neither group.

The boundary is 9.14's, restated: multiple background *layers* remain refused (one layer is
this build's value model), nothing paints a background, no percentage is resolved against a
box, and no image is loaded. One capability joins the table (`blendModes`), two computations
join the closed-vocabulary body, and the module moves to `0.35.0`.

### The border family completed (18.18)

Phase 18.18 completes the border family 9.15 began, the way 18.17 completed the
background one. It adds the sixteen rows `docs/Borders.md` §9 named as not built: the
`border-image` decorations, the `outline` family, and `border-radius` with its four
corners.

Only one of the sixteen takes a shape of its own. `border-image-repeat` is a genuine
closed set — `stretch | repeat | round | space` — and the border-image analogue of the
background repeat set: `round` and `space` are the two adjustments that avoid a partial
tile (scaling, and gaps), and neither is `repeat`. Every other row reuses a shape or is
carried, which is the split every phase since 9.14 uses: `border-image-source` is
`none | <image>` and so reuses 9.14's image shape unchanged; `outline-style`,
`outline-width` and `outline-color` reuse 9.15's border style, the shared line width and
9.9's colour; and the slice, width, outset and radius grammars are wider than any closed
set (a `fill` keyword, a slash-separated pair, a length-percentage pair), so they are
carried.

`border-radius` additionally carries the **corner vocabulary** — four corners, the
module's one spelling of `border-top-left-radius` and its siblings — because a radius is
a geometry (four ellipse quadrants per corner) rather than a closed set, and the phase
that models it is the one with a paint surface. All sixteen rows are non-inherited,
because a border, an outline and an image are painted per element, and `outline-color`
is the fifth row in the table whose initial is the reference `currentcolor`.

The boundary is 9.15's: nothing paints a border, no image is sliced or tiled, and the
radius geometry is left to the layout/paint phase. One capability joins the table
(`borderDecorations`), one computation joins the closed-vocabulary body, and the module
moves to `0.36.0`.

### The filter family (18.19)

The filter family (`RoboCSSFilter.h`) is the first this build types whose **value is an
ordered list of functions whose order is the meaning**: `blur(2px) grayscale(100%)` is
not `grayscale(100%) blur(2px)`. So the property grammar is a carried list and the
phase's typed deliverable is the **name vocabulary** the list is built from — eleven
`<filter-function>` names with the classifications a consumer reads — exactly the role
18.7's `CssTransformFunction` plays for `transform`.

The classifications are three independent questions rather than one tree. The functions
split by what they *do*: a reference (`url`), two **spatial** ones that move pixels
(`blur`, `drop-shadow`) and eight **colour** ones that change pixels in place. `url()`
is in neither of the latter two — it is a reference — so a consumer that read "not
spatial" as "colour" would apply a colour matrix to an SVG filter. And they split by
what they *take*: a `<length>` for `blur` and `drop-shadow`, an `<angle>` for
`hue-rotate`, a `<color>` for `drop-shadow`, and a bare number-or-percentage **amount**
for the seven whose omitted argument defaults to `1` — which is why the grammar has to
allow a function written with no argument at all. The four columns are independent and
not one enumeration: `drop-shadow` takes both a length and a colour, and `hue-rotate`
an angle and no amount.

Two rows join the table (`filter`, `backdrop-filter`), both non-inherited and both
carried, with the initial value `none` — which is a value, not an empty list. No
computation joins the closed-vocabulary body, because no property's grammar *is* a
closed set here: a list is wider than any closed set, and the vocabulary lives where a
consumer can reach it rather than in the normaliser. That is 18.7's split, restated for
filters.

The boundary is stated rather than implied: nothing here blurs a pixel, shifts a shadow,
mixes a colour matrix, fetches an SVG filter or measures anything. A value says *which filter* a renderer
applies; the pixels belong to the phase that has a surface. One capability joins the
table (`filters`), one diagnostic domain (`RoboCssDomainFilter`), and the module moves
to `0.37.0`.

### Colour

A *colour* is the one shape whose meaning is arithmetic, so since 9.9 it is a type
of its own rather than a corner of the computed-value layer:

```text
Color                            one colour
  ├── kind                       NONE / RGBA / CURRENT / INVALID
  └── red, green, blue, alpha    live only when the kind is RGBA
```

Four kinds, and `CURRENT` is load-bearing: `currentColor` is not a colour but a
reference to whatever `color` turns out to be, so it is *typed* and carried rather
than approximated, and `RoboCSSComputed.c` resolves it at the only point that has
the answer — against the parent's computed `color` when the property *is* `color`,
and against the element's own everywhere else. `CssColor` owns nothing: it
is four bytes and a tag by value, so it crosses every boundary — the value layer,
the cascade, inheritance, the computed style — without a lifetime rule. The six
outcomes (`RESOLVED`/`CURRENT`/`DEFERRED`/`MALFORMED`/`UNKNOWN_NAME`/`INVALID`)
exist so that "legal CSS this build cannot represent" stays distinguishable from "a
mistake", because refusing the first would be a lie and accepting the second would
be a wrong colour. `docs/Colors.md` has the full account.

### Unit

A *unit* is a closed vocabulary rather than a string suffix, so since 9.10 it is a
type of its own rather than a corner of the computed-value layer:

```text
Unit                             one unit
  ├── name                       the canonical spelling
  ├── class                      NONE / ABSOLUTE / FONT_RELATIVE / VIEWPORT
  ├── to_px                      the fixed factor, 0.0 when there is none
  └── family + measure           which viewport, and which dimension of it
```

### Box

The box model's *vocabulary* is a table, and since 9.11 it is a type of its own:

```text
Side                             one edge
  └── top / right / bottom / left, in CSS's order

Shorthand                        one declaration that stands for four
  ├── name                       `margin`, `padding`, `border-width`
  └── longhands[4]               indexed by side

map[arity][side]                 the 1/2/3/4 distribution
```

Three rows and a 4×4 table of integers, `static const`, owning nothing. The order of
the sides is the thing the table is *for*: `1px 2px 3px` is a statement about it.

What makes this a module rather than a corner of the inheritance pass is that
expansion and *resolution* are different questions. Which declaration wins an edge is
the cascade's, and 9.11 added exactly one door to say so:
`CssCascadeCandidateCompare` weighs two candidates from one cascade by level,
then specificity, then document order — the cascade's own three keys, restated
nowhere. The box model therefore contains no level arithmetic and no specificity
comparison, and an edge spoken for twice (`margin: 10px` and `margin-left: 20px`) is
decided by the same rules that decide an edge spoken for once.

The expansion pass runs inside `RoboCssInheritResolve`, between "what the element
declares" and "what the parent carries", and it is where a distributed longhand becomes
an ordinary declared property: it goes through the same `inherit_value` a written
declaration goes through, so the CSS-wide keywords mean the same thing on a shorthand as
on a longhand *by construction*. The declared pass skips box-model longhands, which is
what keeps an edge from getting two entries. A shorthand that cannot be distributed —
five components, or a `var()` whose substitution would change the count — leaves its own
entry intact, increments a counter on the resolved style and sets `unsupported`, because
that is the phase's one quiet failure and a consumer must be able to see it.

One table row per unit, and every accessor is a lookup in it, so adding `dvw` is
adding a row rather than adding a branch to four functions. The set is 31 units plus
`NONE`, and the enumerator values are append-only because a stored enumerator is a
contract: 9.8's units keep the numbers they had and 9.10 appends `lh`, `rlh`, `vi`,
`vb` and the twelve `sv*`/`lv*`/`dv*` units.

`CssUnitContext` is what a *relative* unit needs in order to mean anything
— the element's font metrics, the root's, and up to four viewport sizes — and it is
an argument rather than a global, with a `has_` flag per fact so that "the font size
is 16" and "I do not know the font size" stay two different answers. That is what
makes `CssUnitResolvePx` a *question* with four answers rather than a
multiplication: `RESOLVED`, `NEEDS_CONTEXT`, `NOT_A_LENGTH` or `INVALID`. Absolute
conversion needs none of it and is exact by definition, so nothing in this module
knows about DPI. `CssNumeric` is the typed number that carries a unit —
four kinds, kind-aware equality, and one canonical spelling. `docs/Units.md` has the
full account.

### Display

A *display value* is a closed vocabulary, and since 9.12 it is a type of its own:

```text
DisplayValue                     one display value
  ├── name                       the canonical spelling
  ├── outer                      NONE / BLOCK / INLINE
  ├── inner                      FLOW / FLOW_ROOT, and the modes a later phase adds
  ├── legacy                     the single-keyword values, kept whole
  └── level                      the classification a layout phase reads
```

Four values, four rows. The row carries the *two halves* of the value rather than
one enumerator, because CSS Display's model is a pair — an outer display type and an
inner display type — and `inline-block` is really `inline flow-root`. `display: none`
is the degenerate pair with neither half present, which is why it is a decided value
and not the absence of one: an element with `display: none` has an answer, and the
answer is that it generates no box.

Keeping the halves separate is the whole reason the row exists. A future `flex` is a
row, not a branch: `outer: BLOCK, inner: FLEX, legacy: false`. The *legacy* single-keyword
spellings (`inline-block`, `inline-table`, …) are stored whole rather than decomposed
into their halves, because they are what the author wrote and what serialization has to
reproduce; the halves are how the *classification* is answered. `level` is the
one-word summary a layout phase dispatches on — `BLOCK`, `INLINE`, `INLINE_BLOCK`,
`NONE` — and it is derived in the table so two readers cannot disagree about whether
`inline-block` is inline-level (it is, for layout) and block-level (it is, for
containment).

The property is *not* inferred from the element's tag. An `<h1>` is `inline` until a
sheet says otherwise, because the CSS initial value belongs to the property and the
mapping from tag to display belongs to a UA origin that does not exist yet. Nothing in
this module reads an element name.

`docs/Display.md` has the full account.

### Positioning

A *position value* is the second closed vocabulary, and since 9.13 it is a type of
its own:

```text
CssPositioning           one position value
  ├── the value                  STATIC / RELATIVE / ABSOLUTE / FIXED / STICKY
  ├── in_flow                    does the box take up room in its parent
  └── positioned                 is it a containing block for an absolute descendant
```

Five values, five rows. The row carries **two flags rather than one three-valued
kind**, because the two questions are asked by different stages: `in_flow` is the
layout question (does this box take up room), and `positioned` is the containment
question (what does an offset inside it resolve against). They are not each other's
opposite — `relative` and `sticky` answer yes to both — and folding them into one
kind would make every reader compare against a spelling. `is_positioned` is asked of
the table rather than written as `!= STATIC`, so that `position: running()` can
answer for itself on the day it is a row.

The four insets (`top`, `right`, `bottom`, `left`) are deliberately **not** part of
this model. Their grammar — `<length-percentage> | auto` — is the box model's
`EDGE_OFFSET` shape exactly, so they join that family and reuse its normalisation,
and their names come from the side vocabulary in `RoboCSSBox.h`. A second shape with
the same grammar is how `top: 20%` and `margin-top: 20%` would come to disagree.

`z-index` is the first property whose value must be an **integer**, and it is a
shape of its own for that reason: `auto` is a keyword (not zero — "takes no part in
its parent's stacking decisions" is a different statement from "at level zero"),
and everything else is a NUMBER that must carry `is_integer`. `1.5` and `0.0` are
both refused, because CSS's `<integer>` grammar is about the *token* and `0.0` is a
number token even though its magnitude is whole. Nothing in this module compares two
z-indices or computes an order: a stacking context belongs to a later phase.

Nothing about `position` is a behaviour here. `sticky` is a value with the same two
facts as any other, and what a sticky box does relative to a scrollport is a
scrolling question the build has no scrollport for. Refusing the value would be the
other wrong answer, since `position: sticky` is legal CSS an author has written and
dropping the declaration would lose the offsets beside it.

`docs/Positioning.md` has the full account.

### Background

Backgrounds are six longhands and therefore **five models**, not one, because the six
do not share a grammar:

```text
robo_css_background_color_t        the colour family, reused from 9.9 unchanged
robo_css_background_image_t        none / a URL reference the parser kept as tokens
CssBackgroundRepeat       repeat / repeat-x / repeat-y / no-repeat
CssBackgroundAttachment  scroll / fixed
CssBackgroundPosition     two *components*, each edge-or-length-percentage
CssBackgroundSize         auto / cover / contain / two length-percentages
```

Two of the six are **closed vocabularies** built the same way as `display` and
`position`: a row per value, walked rather than switched on. `background-repeat`
threads its rows to the two *axes* it repeats on, so `repeat-x` is not a spelling
that happens to produce `repeat no-repeat` — it **is** that pair, read from the row.

`background-position` and `background-size` are the first properties whose value is a
**pair**, and the pair is where the phase's one real design problem lives. CSS lets a
single component stand for both halves (`center` means `center center`), so the model
distinguishes a *component* — which knows its own kind: an edge keyword, a length, a
percentage — from the *pair* a component was folded into. Folding happens once, at
parse time, so the computed value never has two spellings for one meaning. Equality is
kind-aware: `left` and `0px` are different components however equal they look, because
`50%` must stay a percentage and `0px` must stay a length for the phase that has a box
to resolve them against. `background-size` borrows the same pair and adds the two
keywords `cover` and `contain`, which are *not* `auto`.

`background-attachment: fixed` is a value and not a behaviour — there is no scrollport
to fix against — and `background-image` keeps whatever `url(…)` the grammar produced
without fetching it. `none` is a value rather than a missing image, and
`background-color` reuses the colour family from 9.9 rather than growing a second one.

The `background` shorthand is distributed by the **inheritance layer's** existing
distribution pass, so it reaches the six longhands through the same rules a declared
longhand does, including `initial`/`inherit`/`unset`; unsupported syntax is refused
with a reason rather than half-read.

`docs/Backgrounds.md` has the full account.

### Rule

`CssStyleRule` holds a selector list plus a declaration list.
`CssAtRule` holds a name, a prelude, a block and nested rules.
`CssRule` is a tagged union of the two plus `kind`, `range` and any
metadata, and `CssRuleList` flattens nested rules in document order.

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

As of 9.3 `CssStylesheetParse` fills that rule list for real, and the parse
is transactional against it: `slot->rules` is replaced, never appended to. The
first CSS problem is kept in the sheet's own error slot as well as being handed to
the caller, so a call site that did not want the record still sees it.

### Cascade

```text
Cascade                          what one element gets, in cascade order
  ├── candidates                 borrowed declarations + provenance + level
  │                              + specificity + document order
  └── inline_declarations        owned: parsed from the element's `style` attribute
```

A cascade is a *result*, not a model in the source sense: it owns the array of
candidates and the declaration list parsed from the `style` attribute, and every
candidate borrows the declaration it points at. It is produced by the style
context and read through `RoboCSSCascade.h`; the cascade's own vocabulary — the
level table, the verdicts — is separate from the origin vocabulary, so provenance
stays provenance and priority stays priority. A candidate's `specificity` is the
one of the selector that *matched* it, because a rule's list may have several and
only the matching member is the declaration's reason for being here.

## 6. The DOM boundary

RoboCSS must eventually answer:

```text
DOM Element  →  style resolution  →  computed style
```

9.1 defines the seam and implements none of the resolution:

* `CssElementRef` — an opaque host-supplied element identity. It is a
  pointer or an index as far as RoboCSS is concerned; RoboCSS never dereferences
  it and never assumes it is a node handle.
* `CssElementView` — what a matcher is given *about one element*: its
  ref, tag name, id, class slices and attribute name/value pairs. Nothing about
  the tree, because a view is a description of one element and not of a
  neighbourhood.
* `CssElementHost` (9.4) — one `userdata` and **four** callbacks: `view`
  (required), `parent`, `previous_sibling`, `next_sibling` (all optional). The
  three relation callbacks are separate from the view rather than fields in it,
  because they answer questions about *other* elements and because a host may
  legitimately be unable to answer them at all — which the matcher must be able
  to tell apart from "there is no such element", since `:first-child` is exactly
  "there is no previous sibling". See `docs/Selectors.md` §4.
* The **style context** (`RoboCSSStyle.h`) holds the property table, the
  stylesheets in scope with their origins, the element host and the inline-style
  callback. `CssStyleContextMatchDeclarations` is real as of 9.5 — it
  collects what applies to one element in cascade order (`docs/Cascade.md`) — and
  as of 9.8 `CssStyleContextComputeStyle` is real too: it resolves the
  ancestor chain and computes each level top-down, so the level below inherits a
  value that has already been through the same normalisation
  (`docs/ComputedValues.md`).

The DOM stays authoritative for elements, attributes, tree relationships and text
nodes. RoboCSS is authoritative for CSS source, stylesheets, rules, declarations
and — later — resolution. **RoboCSS does not duplicate the DOM tree**, holds no
node registry, and caches no view across a call.

The host constructs the view, which means the boundary is driven today exactly
the way 9.4's matching, 9.5's collection, 9.7's chain walk and 9.8's computation
drive it, and `css.style.context_borrows` asserts the contract a host has to
satisfy.

## 7. The inline-style boundary

```html
<div style="color: red">
```

The DOM keeps the `style` attribute. RoboCSS parses its *text* into declarations
on request through `CssDeclarationListParseText` — a real parse as of
9.3, the same grammar a rule's block uses — and as of 9.5 the cascade is one of
the callers, asking the host for the text, parsing it, and collecting the result
as `RoboCssOriginInline` candidates. RoboCSS still stores none of it. The seam
enforces:

* declarations produced carry `RoboCssOriginInline`;
* an empty or absent attribute is **no declarations** (not an error, not an empty
  declaration);
* the cleartext is borrowed from the caller and never copied into the model;
* the DOM view RoboCSS is handed is read-only.

There is no second attribute store and no shadow declaration list. If a caller
wants the inline declarations, it asks for them each time — which is correct,
because the DOM may have changed since the last time it asked.

## 8. The default-stylesheet boundary

`CssDefaultStylesheetText` is the runtime-provided (user-agent) origin's
entry point, returning `NULL` with length `0` in this build. That is the honest
representation of "this runtime ships no default styles yet", and a caller must
read it as *no runtime styles* rather than as failure.

`RoboCssOriginRuntime` exists as an origin, `CssOriginIsStylesheetOrigin`
accepts it, and no default stylesheet text is embedded. The plan's "do not add a
large built-in stylesheet now" is honoured literally: there is a boundary, and
behind it there is nothing.

## 9. Origin and the cascade

Origin is both a **provenance** and, since 9.5, half of a **priority**:

```text
RoboCssOriginNone / RUNTIME / USER / AUTHOR / INLINE
```

A stylesheet may only carry `RUNTIME`, `USER` or `AUTHOR`
(`CssOriginIsStylesheetOrigin`); `INLINE` is a declarations-level origin
and is explicitly *not* a stylesheet origin, because an element's `style`
attribute is not a stylesheet and never becomes one here.

The model still assigns no priority: a *level* is a separate vocabulary
(`RoboCSSCascade.h`), computed as `CssCascadeLevelOf(origin, important)`,
so an origin remains a fact about where a rule was written and a level remains a
fact about who wins. Two rows of that table are decisions rather than
transcriptions — inline sits above the origin it belongs to, and `!important`
reverses the origin order without reversing inline's position — and
`docs/Cascade.md` states both and why.

The cascade's three keys are level, then specificity (9.6), then document order,
and all three are now present: 9.5 owns the levels and the ordering, 9.6 owns the
middle key, and the cascade holds no arithmetic of its own — it asks
`CssSpecificityCompare`, which is the single comparison in the runtime. A
contest is therefore always decided; there is no third verdict left to report it
as pending.

Inheritance (9.7) is the key *underneath* the cascade rather than a fourth one:
it never competes with a declaration, so no level and no specificity is consulted
when a value is inherited. A declaration on the parent — even an `!important` one —
reaches a child only because the child's own resolution asked the parent for the
properties it did not declare; there is no path by which the parent's declaration
wins an argument on the child.

## 10. Errors

`CssError` is a *value*, never control flow:

| Field | Meaning |
|---|---|
| `kind` | The CSS problem: `NONE`, `UNSUPPORTED`, `RECOVERABLE_SYNTAX`, `INVALID_TOKEN`, `MALFORMED_DECLARATION`, `MALFORMED_RULE`, `UNEXPECTED_EOF`, `OUT_OF_MEMORY` |
| `code` | The matching `Result`, so a caller keeps switching on the existing vocabulary |
| `position` | Byte offset, and line/column once the source was consulted |
| `message` | A static or caller-lifetime string, never freed by RoboCSS |

`recoverable` (a parser can continue and still produce a stylesheet) is
distinguished from `fatal` (`UNSUPPORTED`, `OUT_OF_MEMORY`). Fatal means "this
attempt is over" — **never** "the process dies". No RoboCSS path terminates the
caller and no entry point fails the process on malformed CSS; malformed input is
carried, described and released.

The parser's cursor keeps the **first** error and does not let a later one
overwrite it, because after a recovery path has skipped three constructs the
useful report is still the first problem. `CssParserTakeError` hands the
record out and clears it. The 9.3 grammar follows the same rule across the whole
parse, and counts recoveries separately: `grammar_fail_at` increments the count
every time and fills the record only the first time. The tokenizer's own error is
folded in the same way and *taken* as it is absorbed, so a second tokenizer
problem is not masked by the first for the rest of the parse.

## 11. Memory and lifetime

* One growable-array rule (`CssGrow`) serves every collection. The
  collections are deliberately *not* a shared generic container type: they hold
  owning elements and are appended by moving into them, and a generic container
  would invite a caller to copy one in.
* The module owns a static, bounded slot table. `CssShutdown` releases every
  live stylesheet, so a suite can assert an exact live count either side of a
  scope.
* Every suite that allocates measures `MemDebugLiveCount()` around its
  scope and asserts it returns to the baseline. `CssShutdown` is also
  asserted to release what it handed out.
* Sanitizers are clean: AddressSanitizer and UndefinedBehaviorSanitizer both run
  over the whole suite with zero findings. See `docs/Tests.md`.

## 12. What the phases so far explicitly do not do

Layout, painting, rendering, animations, transitions, external stylesheet
fetching and networking — plus, within the cascade itself, evaluating at-rules and
the animation and transition origins, and within colours the modern spaces (`hwb`,
`lab`, `lch`, `oklab`, `oklch`, `color()`, `color-mix()`) and interpolation between
colours. **CSSOM is no longer on that list in the wholesale sense it was** — the
model and its text rendering are 18.2's — but the CSSOM's *document* half (its
`styleSheets` list, `getComputedStyle`, a parent-stylesheet chain, a live context
on an edit) is still absent and is named in `docs/Cssom.md` §4. **Flex layout** is
on the list in the same sense: 18.3 adds the family's *values* and their
classifications (§5), but no line is measured, no free space is distributed and no
shorthand is expanded — a value says what rule a layout pass follows, and where the
items land is the phase that has a box (`docs/Flexbox.md` §4).

Phase 18 has begun to move items off that list, and the first is
custom-property substitution (18.1, §5 and `docs/Variables.md`): the one half of
CSS Variables that 9.x named as a boundary and left unsubstituted. It is
deliberately the smallest Phase 18 change that completes an *existing* surface
rather than opening a new one — the data model was already there, and what the
phase adds is the pass that reads it. The second is the object model (18.2, §5
and `docs/Cssom.md`): the text rendering and the mutable style declaration the
architecture had always implied and never had. The third is the flexbox family
(18.3, §5 and `docs/Flexbox.md`): three closed vocabularies and the four shapes the
alignment set is read through — the values a layout phase will consume, added by the
phase that owns them rather than by the layout phase that first needs them. The
fourth is the grid family (18.4, §5 and `docs/Grid.md`): the `grid-auto-flow` closed
set — the first vocabulary here whose values are two words — and the gutter length
family, with the track template and line placement properties registered as carried
declarations until the track grammar is a phase of its own. The fifth is the table
vocabulary (18.5, §5 and `docs/Table.md`): five closed sets describing a table box —
the first family here whose members are *mostly inherited* — read through the shared
closed-vocabulary body, with `border-spacing`'s pair grammar carried and the internal
table display types named as table layout's (19.6). The sixth is the multi-column
vocabulary (18.6, §5 and `docs/MultiColumn.md`): the two closed sets a fragmentation
container behaves by, a count and a width read through the shapes every other number
and length uses, the rule decoration reused from the border family, and the gutter
18.4 already registered. The seventh is the writing-mode family (18.13, §5 and
`docs/WritingModes.md`): the three closed sets that say which physical edge is
inline-start and how a character stands in a vertical line, where vertical and
sideways are two separate answers, and `direction` is the one shape *upgraded in
place* from the bare KEYWORD shape it had since 9.7. The eighth is the transform
family (18.7, §5 and `docs/Transforms.md`): `transform-style` and
`backface-visibility` as closed sets, and the closed set of `<transform-function>`
names a `transform` list is built from with its 3D and matrix classifications — the
list, origin and perspective carried as declarations, and no matrix composed. The
ninth is the transition family (18.8, §5 and `docs/Transitions.md`): the
`transition-behavior` closed set and the keyword half of the timing-function grammar,
with the property/time/function lists carried and no engine or clock. The tenth is
the animation family (18.9, §5 and `docs/Animations.md`): four closed sets —
direction, fill mode, play state and composition — the `animation-iteration-count`
shape (whose bound is a floor, so a fraction is legal), and the transition family's
timing-function keywords reused rather than copied. The eleventh is media queries
(18.10, §5 and `docs/MediaQueries.md`): the type and feature vocabularies, the range
forms of a feature's spelling, and a matcher whose answer is yes, no or *unknown* —
the first Phase 18 goal that adds no registry row, because a media query is a
prelude and not a property. The twelfth is container queries (18.11, §5 and
`docs/ContainerQueries.md`): the `container-type` closed set and a matcher reusing
the media module's own condition model over a container's numbers, with a `normal`
box answering unknown. The thirteenth is logical properties (18.12, §5 and
`docs/LogicalProperties.md`): the logical side/axis vocabulary and the resolver that
maps a role to a physical `CssBoxSide` under a writing mode and direction, with the box
model's logical longhands registered as the same shapes their physical counterparts are.
The fourteenth is the font family (18.14, §5 and `docs/Fonts.md`): nineteen rows
that split by grammar — six closed vocabularies (`font-style`, `font-kerning`,
`font-optical-sizing`, the four `font-synthesis-*` longhands, `font-variant-caps`
and `font-variant-position`) with `font-style` upgraded in place, and the rest
carried with their inner sets typed (`font-stretch`'s keywords-and-percentages,
`font-size`'s size keywords, `font-variant-numeric`'s and `-east-asian`'s axes) —
plus the `@font-face` descriptor vocabulary, where the same spelling means a
different thing and `font-display` is not a property at all. The fifteenth is the
text family (18.15, §5 and `docs/Text.md`): twenty rows whose central fact is that
`white-space`'s six words hide four independent questions (collapse spaces,
preserve newlines, wrap, break after a preserved space) — ten closed vocabularies
(`text-align`, `text-align-last`, `text-justify`, `text-transform`, `white-space`,
`word-break`, `overflow-wrap`, `line-break`, `hyphens`, `text-decoration-style`)
with `text-align`, `text-transform` and `white-space` upgraded in place, and the
rest carried with their inner sets typed (`text-decoration-line`'s set,
`text-overflow`'s string, `text-decoration-thickness`'s length,
`text-underline-position`'s two questions) — and a split in which the decoration's
five rows and `text-overflow` alone do not inherit, because they describe the box
that draws or clips the text. The sixteenth is the image family (18.16, §5 and
`docs/Images.md`): the `<image>` function vocabulary (thirteen functions with a
gradient/repeating/axis/intrinsic-size/resource classification each) beside two typed
property sets (`image-rendering`, `object-fit`) and three carried declarations
(`image-orientation`, `image-resolution`, `object-position`), whose central fact is
that an `<image>` is a description of where pixels come from rather than a file — so
`image-rendering: auto` is not `smooth`, `object-fit: none` preserves the ratio without
scaling, and a gradient has no intrinsic size.

Conformance (9.16) is behind us and it is the one phase that is *not* a subject: it
adds no production code at all, which is why it is the only entry in this list that
could be written before the phase ran. What it adds is the four questions no
per-family suite can ask — that every registered initial value computes under the
row's own `computation` (a drifted row computes `INVALID` on every element of every
document and only a whole-registry walk would say so), that one representative
declaration per property produces one exact value *and kind*, that one realistic
document's every family agrees on the same element, and that malformed, identical
and repeated bytes all behave. It is also the phase that found two capability flags
(`backgrounds`, `borders`) documented by their own phases and asserted nowhere in
the build, which is the general form of the rule this section is about: a flag is a
promise about the build, and only a build-level question tests it.

The box model (9.11) is *partly* behind us and the boundary is exact: the four
edges are a typed value family and the three shorthands are distributed, while no
edge is measured, no margin collapses, `50%` stays a percentage, `auto` stays the
keyword and `border-width: medium` stays a keyword rather than becoming a used
length. `border` is distributed by 9.15 rather than here, because it spans three
kinds and this table describes one kind over four sides — see the border
paragraph below.

Display (9.12) is behind us in the same limited sense, and the boundary is just as
exact: `none`, `block`, `inline` and `inline-block` are typed values that travel the
whole pipeline, `display: none` is a decided answer rather than a geometry, and the
row's outer/inner pair is what a later phase extends. What does not exist is any
*consequence* of the value — nothing builds a box tree, nothing removes a box tree,
nothing lays a box out, and no element is given a display by its tag. `display: inline
flex` and the other multi-keyword spellings are rejected rather than half-read, since
reading them would claim an inner mode this phase does not represent.

Positioning (9.13) is behind us in exactly the same limited sense: the five
`position` values are typed rows on the whole pipeline, the four insets reuse the
box model's `EDGE_OFFSET` shape so a percentage stays a percentage and `auto` stays
the keyword, and `z-index` is an integer refused a fraction. What does not exist is
anything a position value *does* — no containing block is found, no offset is
resolved, no rectangle exists, and no stacking order is computed, so `z-index` is
a typed number nothing compares. `sticky` is a value and not a behaviour, and
`inset` and the `inset-block`/`inset-inline` shorthands are not distributed.

Backgrounds (9.14) are behind us in the same limited sense: the six longhands are
typed values on the whole pipeline, the `background` shorthand is distributed
through the inheritance layer's own pass, and "this element's background is page
white, 50% 25% of its box, not repeating" is a sentence the computed style can say.
What does not exist is any *picture* — nothing paints a rectangle, nothing fetches a
`url(…)`, nothing tiles an image, nothing resolves `50%` against a box, and
`background-attachment: fixed` is a value with no scrollport to fix against.

Borders (9.15) are behind us in the same limited sense, and this is the first phase
whose subject is a *grid* rather than one property or one family: three kinds over
four sides is twelve longhands, and four different declarations can set any one of
them. The distribution of those twelve therefore left the box model's pass for one
owner that can see all four declarers at once — a longhand two passes materialise is
a longhand with two entries — while the box table kept the rows, because a row is
where the relation between a shorthand, its four sides and the twelve names is
written down once. What does not exist is any *edge*: nothing measures a border,
nothing paints one, nothing combines `border-width` with the style's
`generates_border` fact to produce a used width, `border-radius` is not a value this
build has, and `border-width: 50%` computes because 9.8's contract is a value's
*shape* and not a property's *range*. `currentcolor` is the one initial value in the
registry that is a *reference*, and both doors that reach a computed value resolve
it against the element's own colour.

**Reading bytes is not interpreting them,
building a tree is not interpreting it either, answering a question about one
element is not applying a style to it, and computing a value is not using one**:
the tokenizer knows `10px` is a dimension with the unit `px`, the grammar knows a
prelude is component values up to `{`, the matcher knows whether an element
satisfies a selector, the cascade knows which declaration beat which, inheritance
knows *where each value it reports came from*, computation knows what that
value *is* — a length of ten pixels, a colour, a keyword — and 9.9 knows what that
colour *is* in the only sense that matters: which space, at what precision, and
what `currentColor` would have to resolve against. None of them knows how
wide anything is.

Each one exists here as a *declared seam* that answers `RoboNotSupported`, and
`CssGetCapabilities` reports that it is absent. The flags for the tokenizer
(9.2), the parser and inline styles (9.3), selector matching (9.4), the cascade(9.5), specificity (9.6), inheritance (9.7), computed values (9.8), colours
(9.9), units (9.10), the box model (9.11), `display` (9.12), `positioning`
(9.13), `backgrounds` (9.14) and `borders` (9.15) flipped to 1 for
exactly that
reason, and no other flag did. 9.16 flipped none: it is a promise that
*every* flag is what its phase said, so it is asserted by walking the whole struct
field by field rather than by adding a field to it. Each flipped only when its whole job worked end
to end: `cascade` waited for `specificity` rather than claiming an ordering it could
not complete, `inheritance` waited while `computed_values` was 0 because a
*resolved* value is a different thing from a computed one, `computed_values`
flipped as 9.8 landed, and `colors` waited for the colour *module* rather than
riding on the parser 9.8 had to keep inside the value layer. The same rule held four
times after that: `units` waited for the *unit table* rather than for lengths
to parse, `box_model` waited for the cascade's comparison door rather than for an
edge to resolve, `display` waited for the row rather than for a box, and
`positioning` waited for the vocabulary rather than for a coordinate, and `borders`
waited for the twelve longhands to have one owner rather than for an edge to be
drawn. All of those
flags are about a *value* and none of them claims layout — that is why
`box_model`, `display` and `positioning` are separate flags from each other and
from anything a layout phase will add — while
`default_styles` and `stylesheet_loading` stay 0, because the `RUNTIME` origin slot
is still empty and nothing fetches a sheet.

Two boundaries are permanent features of the phases that introduced them rather
than gaps in them, and they are why "is selector matching done?" has a nuanced
answer even though the selector phase is finished. `:hover` and its
state-dependent relatives **parse** and are reported `unsupported` rather than
assumed true or false, because the input state they read does not exist yet — and
a cascade that could not tell "false" from "not decided" would apply the wrong
rules silently. A pseudo-element matches no element, *decidedly*, because an
element is not its own `::before`: generated content does not exist yet, so the
question was answered rather than skipped. A third boundary had that shape in 9.5 —
a contest needing specificity — and 9.6 closed it rather than leaving it, which is
why the cascade's third verdict was **deleted** instead of kept as a documented
outcome that can no longer occur.

A later phase that generates content matches pseudo-elements by giving the matcher
a subject, and that is where it lands.
