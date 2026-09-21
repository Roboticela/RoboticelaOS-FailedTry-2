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
computed value (a `calc()`, a `var()` reference, a custom property's opaque token
sequence), `INVALID` is an *answer* the call reports successfully rather than an
error, and `unresolved` is a flag no kind can express. The container it lands in is
`CssComputedStyle` — the 9.1 container, unchanged in shape, with each
entry now also carrying its name, its typed value and a provenance bitset.
`docs/ComputedValues.md` has the full account.

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

Layout, painting, rendering, CSSOM, animations, transitions, external stylesheet
fetching and networking — plus, within the cascade itself, evaluating at-rules and
the animation and transition origins, and within colours the modern spaces (`hwb`,
`lab`, `lch`, `oklab`, `oklch`, `color()`, `color-mix()`) and interpolation between
colours.

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
