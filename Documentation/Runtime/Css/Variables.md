# RoboCSS custom properties and `var()` (Phase 18.1)

This document is the authority for the half of CSS Variables that Phase 9 named
as a boundary and Phase 18 completes: a custom property (`--x`) was already a
first-class declaration — an opaque token sequence, ordered by the cascade and
copied down by inheritance — and a `var()` reference was recognised, carried
intact and flagged `unresolved`, but nothing ever **substituted** it. This phase
substitutes it.

The module is
`Runtime/Source/RoboCSS/RoboCSSVariable.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSVariable.h`; the pass is run by
`CssStyleContextComputeResolvedStyle` in `RoboCSSComputed.c`, after the
per-property pass and before `currentColor` is settled.

## 1. Where it runs, and why there

```text
cascade → inheritance (shorthands expanded, custom properties resolved)
        → compute (per property)          ← 9.8
        → substitute var() references      ← 18.1 (this document)
        → resolve currentColor             ← 9.9
```

Two ordering facts are the whole design. Substitution runs **after** the cascade
and inheritance, because it resolves against the custom properties those stages
settled — a reference to `--brand` is a reference to whatever won the cascade
for that name on this element. It runs **before** `currentColor` is resolved,
because a `var()` can *produce* a `currentColor`: `color: var(--brand)` where
`--brand: currentColor` must reach the colour resolution as the contextual
colour it is, not as a reference.

Substitution is a **pure function** of (the value, a computed style carrying the
custom properties in scope). It reaches no DOM, no font, no box and no parent;
that is what makes one pass serve `color: var(--brand)` and `width: var(--w)`
alike, and it is why the module can be tested without a document.

## 2. The rules it implements

| Case | The answer |
|---|---|
| `var(--x)` and `--x` is defined | the referenced **value**, substituted |
| `var(--x, <fallback>)` and `--x` is undefined | the fallback, substituted |
| `var(--x)` and `--x` is undefined | invalid at computed-value time → the property follows `unset` |
| `--x` is empty (`--x: ;`) | the empty value; `var(--x)` is legal and yields nothing |
| a cycle (`--a: var(--b); --b: var(--a)`) | answered as "not found": the fallback if one was written, `unset` otherwise |
| a reference nested in a function (`calc(var(--x) + 1px)`) | substituted in place; the `calc()` is then carried whole |
| a custom property that is itself a reference (`--a: var(--b)`) | resolved at use time, to the end of the chain |

Two of those are worth stating again in full.

**A substitution is a token sequence, not a token.** `--m: 10px 20px` makes a
reference to it a two-component value. The result of the pass is therefore a
component-value tree that mirrors the value it replaced, with leading and
trailing trivia dropped (the whitespace around a custom property's value or
around a comma-separated fallback is the *separator* that spelled it, not part
of it).

**Invalid at computed-value time is not `INVALID`.** The declaration was legal;
it referenced something nothing supplied. So the property follows `unset` — the
parent's computed value for an inherited property, its own initial value
otherwise — and is *not* marked invalid. A build that returned a failure code,
or an empty value, would be choosing between dropping the declaration and
inventing one for it, and neither is true.

## 3. Cycles and the depth bound

An undefined reference, a cyclic one and a chain deeper than
`RoboCssVariableMaxDepth` (32) all answer **not found**, because all three mean
"this reference has no value here". The stack is a *depth* bound rather than a
visited set precisely so that a long, legal chain still resolves: a cycle is a
name already on the stack and is caught immediately, while a deep-but-acyclic
chain is the case that must not be answered by a stack overflow.

Nothing is cached. A custom property is read through the same computed-style
lookup every other consumer uses, so a mutation cannot leave a substituted value
behind — there is no cache to go stale.

## 4. The boundary this phase names

The inheritance pass expands a **shorthand** before this pass runs. A shorthand
whose component count a substitution would change — `margin: var(--m)` — is
therefore left with its own single entry and takes `unset` at computed-value
time, rather than being distributed into four edges. That is a pass-order limit,
not a missing feature: CSS substitutes *before* it expands, and lifting this is a
change in the phase that owns expansion. It is asserted as its own case
(`css.architecture.shorthand_boundary`).

## 5. Capabilities and version

Phase 18.1 adds two flags to `CssCapabilities` — `customProperties` and
`variableSubstitution` — and takes the module to **0.19.0**. Both are asserted
field by field in `SuiteRoboCssConformance.c`, and read again in
`SuiteRoboCssArchitecture.c`.

## 6. Tests

`Tests/Css/SuiteRoboCssArchitecture.c` (`css.architecture.*`, 11 cases) is the
contract: the two capability flags, `CssValueHasVarReference` over built values
(including a reference nested in a `calc()`), and every resolution rule above —
a defined reference, a fallback, a chain, a cycle, and the two shapes of
`unset` (a non-inherited property taking its initial value, an inherited one
taking the parent's computed value), plus determinism and the shorthand
boundary.

```
pwsh -NoProfile -File Scripts/Test/TestCss.ps1 -Target Host
```

A change to the substitution pass is not done until `TestCss.ps1 -Target Host`
passes and then `TestLayout`, `TestPaint`, `TestHtml` and `TestDesktop`
(`-Target Host`) pass, and `Build.ps1 -Module Css -Verify` reports the module's
Chromium-independence scan `OK`.
