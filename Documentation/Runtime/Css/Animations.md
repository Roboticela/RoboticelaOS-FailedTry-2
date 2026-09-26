# RoboCSS and the Animation Family (Phase 18.9)

This document is the authority for the animation family Phase 18 adds to RoboCSS.

The module is `Runtime/Source/RoboCSS/RoboCSSAnimation.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSAnimation.h`.

## 1. What the phase is

An animation is nine longhands and a shorthand. Four are closed sets and one is a
numeric shape of its own:

```text
animation-direction     normal | reverse | alternate | alternate-reverse
  └── reverses            reverse, alternate-reverse
  └── alternates          alternate, alternate-reverse
animation-fill-mode     none | forwards | backwards | both
  └── fills forwards      forwards, both
  └── fills backwards     backwards, both
animation-play-state    running | paused
  └── is running          running
animation-composition   replace | add | accumulate
  └── combines            add, accumulate
animation-iteration-count   infinite, or a non-negative number
```

Two facts are the substance. `animation-direction` carries **two** classification
columns because reversing and alternating are different questions: an alternating
animation reverses only on the *odd* iterations, so a consumer must ask both.
`animation-iteration-count` accepts a **fraction** — half an iteration is meaningful —
so its bound is a floor rather than CSS's usual whole-number rule: a fraction is a
value and only a negative count is a mistake. `infinite` arrives as a keyword (the
NUMBER kind has no infinity), which is why the shape's two halves are different kinds.

`animation-timing-function` is the **transition** family's timing-function keywords
(§18.8) reused, not a second copy, exactly as `column-rule-*` reuse the border shapes.

## 2. What is carried

| Property | Initial | Why carried |
|---|---|---|
| `animation-name` | `none` | a keyframes name list |
| `animation-duration` | `0s` | a time list |
| `animation-delay` | `0s` | a time list |
| `animation-timing-function` | `ease` | a keyword-or-function list |
| `animation` | `none 0s ease 0s 1 normal none running` | the shorthand |

A `@keyframes` at-rule is the *source* an animation name refers to, not a property:
this module supplies the vocabulary a name is checked against and does not parse the
at-rule (that is the parser's).

## 3. Non-goals

- **No animation engine and no clock.** Nothing starts an animation, resolves a
  keyframe, samples an easing curve, interpolates a value or advances a timeline.
- **No `@keyframes` parsing.** See §2.
- **No CSS Animations Level 2 `animation-range` / scroll timelines.** Named, not built.
- **No interpolation.** That is the module that owns a clock.

## 4. Capabilities and version

Phase 18.9 adds one flag to `CssCapabilities` — `animations` — and one diagnostic
domain (`RoboCssDomainAnimation`), and takes the module to **0.28.0**. Ten registry
rows join the table, none inherited: four typed vocabularies, the iteration-count
shape and five carried grammars. The registry count moves from 130 to 140, non-inherited
from 108 to 118, inherited stays 22.

## 5. Tests

`Tests/Css/SuiteRoboCssAnimation.c` (`css.animation.*`, 8 cases) is the contract:

- the four closed vocabularies walked in enumerator order, with every keyword read
  back, the case-insensitive match, the absent/unknown `FromText` answers and the
  `SliceIsKnown` door;
- the classifications of all four, including that **reverses and alternates are two
  separate answers** for `animation-direction` and that `both` fills in both
  directions, with the safe answer for an unknown enumerator;
- the iteration-count shape: `infinite`, a whole count, a **fraction**, and the one
  refusal (a negative count);
- the four vocabulary shapes through the real grammar and the refusals of `banana`;
- the ten registry rows, their shapes (including the shorthand's concatenation) and
  their non-inheritance;
- the capability flag, the version, the computation names and the diagnostic domain;
- determinism.
