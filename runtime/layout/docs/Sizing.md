# Width and height (Phase 10.8)

How big a box is: what its declarations asked for, what its containing block
offers, what its contents turned out to be, and the one arithmetic that turns those
into the `size` a result carries.

This is the phase where the `width` RoboCSS has parsed since 9.11 finally *means*
something, and where the `height` 10.3 could already read stops being read by 10.3.
It is also the first phase in this module that **moves an existing expectation on
purpose** — three suites record a declared `width` being ignored, and all three say
in their own comments that the phase which resolved one must update them.

Files: `Include/RoboLayout/RoboLayoutSizing.h`, `Source/RoboLayout/RoboLayoutSizing.c`.
Suite: `layout/tests/SuiteRoboLayoutSizing.c` (17 cases).

## 1. The one structural decision: a declared width is what a box's *children* are laid out into

10.3's recursion handed every block-level child its containing block's content
width, and then gave the box itself that same width. One number, used twice. A box
that declared `width: 300px` inside an 800px parent therefore came out 800 wide
*and* put its children in 800 — and the suite for 10.3 recorded that as the honest
answer of a build with no sizing layer.

The sizing layer splits those two uses, and the split is the phase:

```
containing block's content width
        │
        ▼
   resolve the WIDTH          ← the declared one, or the available one
        │
        ▼
   lay the children out INTO that width
        │
        ▼
   resolve the HEIGHT         ← the declared one, or the contents' extent
```

For an `auto` width the two numbers are the same — `auto` *is* "fill the containing
block" — which is why every 10.3 expectation about a box with no declared width is
untouched, and why the phase could add real sizing without rewriting a single
existing geometry assertion. The two cases agree exactly where the old build knew
only one of them.

The ordering is what the two dimension resolvers exist for.
`robo_layout_sizing_resolve` is the pair, and it is the shape a block box uses.
An **atomic inline** cannot use it: its `auto` height is the block extent of its own
contents, and those contents can only be laid out *at a width*, so the width has to
be settled first, the contents walked, and the height asked for afterwards
(`RoboLayoutInline.c`). A single door would have forced that caller to either lie
about contents it does not have yet, or resolve twice and throw one answer away.

## 2. Three kinds of dimension, not a number with a flag

```
ROBO_LAYOUT_DIMENSION_AUTO         no declaration, or the keyword `auto`
ROBO_LAYOUT_DIMENSION_DEFINITE     a px length this build can use
ROBO_LAYOUT_DIMENSION_UNRESOLVED   declared, and not resolvable here
```

CSS's initial value for `width` and `height` is `auto`, so an element nothing
declared one for *is* `auto`; there is no separate case for "absent" and the header
does not invent one.

`UNRESOLVED` is the kind that earns its existence. A percentage and `auto` resolve
**identically today** — fill the containing block — and are stored differently
because that is the answer that will change: a percentage needs a containing
*dimension*, and the day one is known the branch that changes is the resolver's,
not the seam's that read the declaration. Folding `50%` into `auto` at the seam
would have made that change a rewrite, and would have lost the fact that somebody
asked for something this build cannot honour.

Three decisions follow from the kinds:

- **A cleared record is `auto`, not a zero length.** `robo_layout_dimension_init`
  writes `AUTO` with `length = 0`, and nothing reads `length` without the kind
  check — because `0` is a size an author can ask for, and a default that meant both
  would be read as one.
- **A negative length is refused where it is *stored*, not clamped where it is
  used.** `robo_layout_dimension_is_valid` is a predicate;
  `robo_layout_sizing_set_width` refuses an invalid dimension. Nothing in layout can
  mean anything by a negative width, and this is the one moment the mistake is cheap
  to see. (`robo_layout_dimension_definite` itself does not validate, for the same
  reason `robo_layout_size_t` does not: a maker is not a door.)
- **A length read from a non-`DEFINITE` dimension is a *miss*, not a zero.**
  `robo_layout_dimension_length` answers `ROBO_NOT_FOUND` and leaves its out
  untouched, which is a different answer from a length of zero and the one a caller
  needs in order to know it has nothing to use.

## 3. The record, and why availability is per direction

`robo_layout_sizing_t` carries all ten numbers the phase is about, in the order a
reader asks about them: available, specified, bounds, contents, resolved — plus a
source and a clamped flag per dimension.

The flags are **per direction**, and that is the phase's answer to what a block box
is: it is given a width and no height at all, because a block's height is its
contents', never its containing block's. One flag over one size would have had to
claim the available height was zero, and "the containing block offers nothing" and
"nobody has told me" are exactly the two inputs 10.1 built `available` a separate
field to keep apart.

`has_available_height` therefore stays clear for every document this build can lay
out. It exists because `height: 50%` will one day have something to resolve against
and this is where that starts — and leaving it clear is how the build says so
rather than assuming zero.

The record is **not a cache**. It is filled by whoever has the inputs, resolved on
demand, and discarded with the stack frame that made it; nothing in the module holds
one between passes. That is what keeps the pass a pure function of the tree
(`RoboLayoutBlock.c` says so) and what makes re-resolution a *property* rather than
a feature: change an input, resolve again, get the new answer and no stale half of
the old one. §10 says what that is worth.

## 4. Where the numbers resolve, and the two failures

```
width   DEFINITE    the declared length          source DECLARED
        AUTO        the available width          source AVAILABLE
        UNRESOLVED  the available width too      source AVAILABLE
height  DEFINITE    the declared length          source DECLARED
        AUTO        the contents' extent         source CONTENT
        UNRESOLVED  the contents' extent too     source CONTENT
```

Two failures, and they are **different failures**:

- an `auto` width with **no available width**: a caller that did not say how wide
  the containing block is has not given this layer an input, and a zero-sized box
  would be the silent wrong answer.
- an `auto` height with **no contents extent**: a caller that has not laid the
  children out yet has asked a question that has no answer *yet*.

Both report `ROBO_NOT_FOUND` with a message that says which, because a caller
debugging one of them should not have to read the other's code. A **known zero** is
neither: an empty box's contents reached 0, and 0 is an answer. A viewport of 0 is
likewise a known zero — accepted by the setters, refused only for a negative extent.

The **source** is recorded beside the number because the same number answers three
different questions: a `width: 300px` and an `auto` width inside a 300px containing
block both resolve to 300, and a caller reading only the size cannot tell whether
the box obeyed its author or happened to fill what it was given. The stats records
count the same distinction in aggregate (`widths_declared` / `widths_filled`,
`heights_explicit` / `heights_from_content`); this is the per-box form.

`robo_layout_sizing_resolved` reports a miss until **both** dimensions have been
resolved, and it reads that off the sources rather than a pair of flags — a flag
beside the source would be two fields that had to agree, and the day they disagreed
the record would have two answers to one question.

## 5. The bounds are 10.1's, and what this phase adds is where they come from

`RoboLayoutConstraints.h` states that "min wins over max" is decided once, in
`robo_layout_constraints_clamp`. That goal reached this one with its promise intact:
the resolvers fill a constraints record from the sizing record's bounds and call it.
What 10.8 decides is only *what* bounds a box has and what happens when they
contradict each other — and the answer to the second is **report it**
(`ROBO_NOT_FOUND`), not resolve it in either's favour, because the conflict is a
caller's to fix and picking a size would hide it.

The bounds *come from* `min-width`, `max-width`, `min-height` and `max-height`,
read through the same seam as `width` and `height`. **RoboCSS registers none of
them**, so for every document this build can parse there is no entry and no bound
recorded — which is why the pass is written as four reads that usually find nothing
rather than as a constant that says nothing exists. The day 9.17 or 10.9 registers
those names, boxes start obeying them with no change to either pass, and the
clamping path is already tested end to end (§8).

**A bound is recorded only when *both* of its dimensions name a usable length.**
10.1's record holds a bound as one `robo_layout_size_t`, so a half-known pair would
bind the other dimension to whatever the unknown half defaulted to — a `max-width`
alone turning every height into zero. The alternative was a sentinel for "unbounded",
which is precisely what 10.1 refused. The case that asserts this
(`layout.sizing.block_half_bound`) is written as an *outcome*: a `max-width` on its
own leaves a 300px box 300 wide and a 60px-tall box 60 tall.

## 6. The CSS seam, and the two readers over one classification

One function turns a computed style into a size in this module:
`robo_layout_sizing_read_dimension(style, property, out)`. It is deliberately four
checks rather than a resolver — the argument is 10.3's, which wrote them:

- the entry must exist (an absent property is `AUTO`, by CSS's initial value);
- a `KEYWORD` spelling `auto` is `AUTO`, and any other keyword is `UNRESOLVED` —
  no other keyword names a length, so `width: inherit` reaching here is a value this
  build cannot use rather than one it may reinterpret;
- a `LENGTH` in `px`, finite, non-negative and inside the geometry's range is
  `DEFINITE`. 9.8's computed rule normalises an *absolute* unit to px exactly, so
  `1in` arrives as `96` and a relative unit does not;
- a `PERCENTAGE`, a relative `LENGTH` or an out-of-range number is `UNRESOLVED`.

`robo_layout_sizing_read_dimension` **fills `out` only when it can answer**: a NULL
style or property leaves it as it found it, which is why the pass can initialise a
record and read four properties in a row without a branch between them. That is the
contract and not sloppy NULL handling — a box with no computed style (a tag the
style source answered nothing for) has made no declarations, and the record the
initialiser wrote already says `auto`.

### 6.1 The reader that had to stay, and why there are two

`robo_layout_style_px_length` was 10.3's, moved here, and kept — because
`font-size` needs it (`RoboLayoutText.c`) and a font size is **not a box
dimension**: `12.5px` has to stay `12.5`, while a declared `width: 10.5px` has to
become one of two integers. Truncating that would make every fractional declared
width come out a pixel short of what the author wrote.

Both readers are shaped from one classification, `layout_style_read_px`, which
answers *what does this declaration hold?* — four kinds, including the `ABSENT` /
`AUTO` distinction the two readers collapse differently. One classification and two
shapes is the arrangement 10.3 arrived at for its property reader and 10.7 for its
baseline rules: a fact computed once and read twice, so the day it changes there is
one place to change it.

## 7. One question, asked from both directions

`robo_layout_object_sizing` fills a sizing record from one layout object: available
width, declared dimensions, bounds. It is defined by `RoboLayoutBlock.c` and
declared in `RoboLayoutInternal.h`, because a block box and an atomic inline ask
**the same question** and two implementations of it would answer differently the
day either gained a property read. What differs between the two callers is only the
containing block, which is the parameter.

## 8. What the suite asserts

Seventeen cases in three groups — the vocabulary, the record, the pass — and the
pass group drives a **fabricated computed style** rather than a stylesheet. That is
not a shortcut: no property RoboCSS registers feeds the clamping path, so the only
way to prove it is *live* rather than merely present is to hand the pass a style
that declares `min-width` and its siblings. A case built from a stylesheet could
only prove that a property the property table does not know does nothing, which is a
fact about the table.

The suite also finds elements **by tag** rather than by arena index, because
RoboHTML's tree builder inserts an implicit `<head>`: a fixture's *indices* are a
fact about the parser, and counting them would be asserting the wrong thing the day
the parser gained one more element. The first cut of this suite made exactly that
mistake and reported a declared width "not reaching geometry" when what it had
actually measured was `<body>`.

The invariant worth reading is `layout.sizing.line_regression`: over a document that
really wraps, every line's width is what it *placed* (150, then 90) where the width
it had to fit into is 200. A build that had let the sizing layer turn every line box
into an independent CSS box would make the first line 200 wide, which is a
plausible number and the wrong document.

## 9. What the phase changed in the suites that came before it

Three cases recorded a declared `width` being ignored, and all three had written the
assertion so that the phase resolving one would have to update them:

| Case | Was | Is |
|---|---|---|
| `layout.block.pipeline` | card `width: 200px` → 800 | → **200** |
| `layout.conformance.pipeline` | card → 1280 | → **200** |
| `layout.conformance.pipeline` | `.title` (the card's child) → 1280 | → **200** |

The third is the phase, seen one level down: a build that resolved a box's own width
and then handed its children the containing block's would pass the first two and put
the title at 1280. Four other assertions moved and none was weakened: the version
pin, the capability table in two suites, and `ROBO_LAYOUT_DOMAIN_COUNT`.

## 10. Re-layout, and why it needed no machinery

Changing an available width, a declaration or the contents' extent and resolving
again produces the new geometry, because nothing is cached and the answer is a
function of the record. `layout.sizing.reresolve` asserts it directly — a changed
available width, then a declared width arriving on top of a filled one (source and
all) — and resolves five times over unchanged inputs to show the answer is stable.
The live inline and full-suite re-runs cover the pass-level version.

There is deliberately **no invalidation system**: a caller re-runs a pass, which is
already what every suite does and what `robo_layout_context_run` is for.
Correctness first; a granular invalidation scheme is a later phase's, and this one
would have had nothing to invalidate.

## 11. What is deliberately missing

Each a named later goal, and each stated where a reader will look for it:

- **percentages resolved against a containing dimension.** They are read, recorded
  as `UNRESOLVED`, and filled from the containing block. What is missing is a
  *known* containing dimension to resolve them against — the same missing input that
  keeps `has_available_height` clear.
- **intrinsic sizing**: `min-content`, `max-content`, `fit-content`, shrink-to-fit.
- **`box-sizing`**, and any contribution from a **border** (10.11). A margin never
  contributes to this layer (10.9) and neither does a padding *inside* it (10.10):
  what the padding changes is the number the caller hands this layer as available,
  and the box the caller derives from its answer. That is why the padding layer,
  not this one, is where `content + padding` is computed (`Padding.md` §4).
- flex, grid and table sizing, and positioning (10.12), which is where `auto`
  insets and a contained box's offset belong.

The one thing this phase does **not** change: `RoboLayoutResult.h`'s statement that
`size` is the border-box size. Sizing decides what that number is; it does not
redefine it.

*10.10's outcome row:* the sentence above needed one clarification rather than a
change. This layer resolves *a box's own box as its declarations determined it*, and
under this build's `box-sizing: content-box` — CSS's initial value — that is the
**content** box. The border box `RoboLayoutResult.h` documents is that plus the
padding, and the padding layer derives it from this layer's answer rather than this
layer resolving it. So no rule here moved; what moved is that the two numbers are now
distinguishable in a document that declares a padding, which is why the *result*'s
`size` and the *resolved* size are the same number only where no padding exists.
