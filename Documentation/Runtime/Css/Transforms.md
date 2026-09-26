# RoboCSS and the Transform Family (Phase 18.7)

This document is the authority for the transform family Phase 18 adds to RoboCSS: the
two closed sets a transformed box is described by and the closed set of function
names a `transform` list is built from.

The module is `Runtime/Source/RoboCSS/RoboCSSTransform.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSTransform.h`.

## 1. What the phase is

Three closed sets, each a row per value:

```text
transform-style       flat | preserve-3d
  └── 3D context        preserve-3d
backface-visibility   visible | hidden
  └── shows back face   visible
<transform-function>  matrix, matrix3d, translate, translate3d, translateX,
                      translateY, translateZ, scale, scale3d, scaleX, scaleY,
                      scaleZ, rotate, rotate3d, rotateX, rotateY, rotateZ,
                      skew, skewX, skewY, perspective
  └── is 3D             matrix3d, translate3d, translateZ, scale3d, scaleZ,
                        rotate3d, rotateX, rotateY, rotateZ, perspective
  └── is matrix         matrix, matrix3d
```

`transform-style` and `backface-visibility` are ordinary keyword vocabularies with the
one classification a rendering pass reads. `preserve-3d` is the fact that makes a box
composite its children in one 3D space rather than flattening each into its plane.

## 2. The function-name set is the substance

The `transform` property itself is a **grammar** — an ordered, comma-separated list of
functions with arguments — and this phase does not parse it. What it supplies is the
closed set of names that grammar is built from, and the two classifications a consumer
dispatches on:

- `CssTransformFunctionIs3D` says whether a function operates in 3D, so a consumer
  knows whether the list needs the 3D machinery at all.
- `CssTransformFunctionIsMatrix` says whether a member already fixes the whole
  transform (`matrix`, `matrix3d`), which is the one case where the rest of the list is
  just a composition of a finished matrix.

That set is what `transform: frobnicate(1)` is refused against — the difference between
"an author wrote a function this build cannot compose" (which a later pass may carry
and report) and "the author wrote something that is not a function at all".

## 3. What is carried

Seven properties are registered as **carried declarations**, because each is a grammar
this phase names rather than parses:

| Property | Initial | Why carried |
|---|---|---|
| `transform` | `none` | a list of functions with arguments |
| `transform-origin` | `50% 50%` | a `<position>` with an optional Z |
| `perspective` | `none` | a length or `none` |
| `perspective-origin` | `50% 50%` | a `<position>` |
| `translate` | `none` | an independent transform |
| `rotate` | `none` | an independent transform |
| `scale` | `none` | an independent transform |

This is the same boundary `border-spacing`'s `<length>{1,2}` pair and the grid track
templates state: a value that is a grammar is carried, and the phase types what it can
rather than growing a parser for a reader that does not exist yet.

## 4. Non-goals

- **No matrix library and no transform layout.** Nothing composes a list, multiplies
  two matrices, resolves an origin or applies a transform to a box. A value says *which
  function* an author wrote and *what kind of transform* the box is in; the module that
  has a renderer composes it.
- **No `transform` list grammar.** See §2/§3.
- **No interpolation.** Transform interpolation is the transitions/animations phase's
  (18.8/18.9), and it consumes the values here rather than re-deriving them.

## 5. Capabilities and version

Phase 18.7 adds one flag to `CssCapabilities` — `transforms` — and one diagnostic domain
(`RoboCssDomainTransform`), and takes the module to **0.26.0**. The flag claims the two
closed sets and the function-name vocabulary with its two classifications; it does not
claim transform layout.

Nine registry rows join the table, none inherited: a transform is the box's own, and a
child that inherited its parent's would move with the parent rather than with the
layout. Two rows take the typed vocabularies, seven are carried. The registry count
moves from 115 to 124, inherited stays 22, non-inherited from 93 to 102.

## 6. Tests

`Tests/Css/SuiteRoboCssTransform.c` (`css.transform.*`, 7 cases) is the contract:

- the two closed keyword vocabularies walked in enumerator order, with every keyword
  read back, the case-insensitive match, the absent/unknown `FromText` answers and the
  `SliceIsKnown` door;
- the classifications (`creates 3D context`, `shows back face`) and the safe answer for
  an unknown enumerator;
- the 21 function names, including the `*3d` and Z forms, and the two classifications
  read per function with exactly the two matrix forms;
- the two typed shapes through the real grammar and the refusal of `banana`;
- the nine registry rows, their shapes, their non-inheritance and their initials;
- the capability flag, the version, the computation names and the diagnostic domain;
- determinism.
