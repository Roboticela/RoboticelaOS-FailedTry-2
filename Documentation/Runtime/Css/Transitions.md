# RoboCSS and the Transition Family (Phase 18.8)

This document is the authority for the transition family Phase 18 adds to RoboCSS.

The module is `Runtime/Source/RoboCSS/RoboCSSTransition.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSTransition.h`.

## 1. What the phase is

A transition is which properties animate, how long, with what easing and after what
delay. Three of the five declarations are **grammars**, so the family is mostly
carried; the phase supplies one closed set and one keyword vocabulary:

```text
transition-behavior   normal | allow-discrete
  └── allows discrete   allow-discrete
timing-function keyword subset
    ease | linear | ease-in | ease-out | ease-in-out | step-start | step-end
  └── is discrete step  step-start, step-end
```

`transition-behavior: allow-discrete` is what lets a *discrete* property (one with no
intermediate values, like `display`) transition at all; `normal` is the initial. The
timing-function keywords are the whole `ease`/`step-*` family, with the one
classification a consumer reads: whether the easing is a **discrete step** that jumps
rather than interpolates.

The timing-function vocabulary is deliberately the **keyword half** of the property's
grammar. The property also admits `cubic-bezier()`, `steps()` and `linear()`, and those
functional notations are carried rather than represented — so the set here is what a
consumer reads to classify an easing, not a claim that the whole grammar is parsed.

## 2. What is carried

| Property | Initial | Why carried |
|---|---|---|
| `transition-property` | `all` | a list of property names, `none` or `all` |
| `transition-duration` | `0s` | a time list |
| `transition-timing-function` | `ease` | a keyword-or-function list |
| `transition-delay` | `0s` | a time list |
| `transition` | `all 0s ease 0s` | the shorthand that concatenates all four |

This is the same boundary `border-spacing`'s pair and the grid track templates state:
a value that is a grammar is carried, and the phase types what it can.

## 3. Non-goals

- **No transition engine and no clock.** Nothing starts a transition, samples an easing
  curve, interpolates two values or advances a timeline. A value says *what easing* and
  *what discrete rule* an author wrote; the module that owns a clock reads it.
- **No functional timing notation.** `cubic-bezier()`, `steps()` and `linear()` are
  named here, not parsed; `transition-timing-function` is carried.
- **No interpolation of computed values.** That is the module that owns a clock.

## 4. Capabilities and version

Phase 18.8 adds one flag to `CssCapabilities` — `transitions` — and one diagnostic
domain (`RoboCssDomainTransition`), and takes the module to **0.27.0**. Six registry
rows join the table, none inherited: one typed row and five carried grammars. The
registry count moves from 124 to 130, non-inherited from 102 to 108, inherited stays 22.

## 5. Tests

`Tests/Css/SuiteRoboCssTransition.c` (`css.transition.*`, 6 cases) is the contract:

- the seven timing-function keywords walked in enumerator order, with every keyword
  read back, the case-insensitive match and the discrete-step classification;
- the `transition-behavior` closed set and its classification, with the safe answers;
- the typed shape through the real grammar and the refusal of `banana`;
- the six registry rows, their shapes (including the shorthand's concatenation) and
  their non-inheritance;
- the capability flag, the version and the diagnostic domain;
- determinism.
