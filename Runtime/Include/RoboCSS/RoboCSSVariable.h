/*
 * RoboCSS custom-property substitution (Phase 18.1).
 *
 * A custom property (`--brand`) is an *opaque token sequence*: the parser keeps
 * it verbatim, the cascade orders it like any other declaration, and inheritance
 * copies it down (RoboCSSPropertyRegistry.h, RoboCSSInherit.h). What 9.x never
 * did — and what this phase adds — is the second half of CSS Variables: a
 * `var()` function is a **reference**, and a declaration that carries one is
 * substituted with the referenced custom property's value before the value is
 * *computed*.
 *
 * This header is that substitution and nothing else. It is a pure function of
 * (the value to substitute, a computed style carrying the custom properties in
 * scope): it reaches no DOM, allocates no node, reads no font and knows no
 * property name. That is deliberate — substitution happens *before* a property's
 * own shape looks at the value, so the same pass serves `color: var(--brand)`
 * and `width: var(--w)` and the value layer beneath neither has to know a
 * reference exists.
 *
 * The three facts a reader needs, because each is a place this could go wrong:
 *
 *   - **A reference is not a value.** `var(--x)` where `--x` is undefined and no
 *     fallback was written is *invalid at computed-value time*: the declaration
 *     is not "empty", it is a declaration that produced nothing, and the caller
 *     (RoboCSSComputed.c) turns that into `unset` rather than into an empty value.
 *     This door reports it as `RoboNotFound` so the caller can tell the two apart.
 *
 *   - **Cycles are invalid, not infinite.** `--a: var(--b); --b: var(--a)` (and a
 *     property that resolves to itself through a fallback) is caught by a bounded
 *     name stack and answered as *not found*, so a cycle takes the fallback if
 *     there is one and is invalid-at-computed-value-time otherwise. The bound is
 *     `RoboCssVariableMaxDepth`, and it is a *depth* bound rather than a visited
 *     set so that a long, legal chain of references still resolves.
 *
 *   - **A substitution is a token sequence, not a token.** `--m: 10px 20px` makes
 *     `margin-left: var(--m)` a two-component value, which is why the result of
 *     this pass is a component-value tree and not a single component: collapsing
 *     it here would lose the shape the property's own normaliser needs.
 *
 * What this build deliberately does not do, and says so: a shorthand whose
 * substitution would change *how many* components it has (`margin: var(--m)`)
 * is expanded by the inheritance pass before this one runs, so it keeps its own
 * single entry with the reference intact rather than being distributed into
 * four edges. The substitution here still resolves it to a value for the
 * shorthand's own name; distributing it is a pass-order change named in
 * `Documentation/Runtime/Css/Architecture.md`.
 */

#ifndef ROBO_CSS_VARIABLE_H
#define ROBO_CSS_VARIABLE_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The deepest chain of custom-property references this build will follow.
 *
 * A legal chain is followed however long it is; this bounds the *recursion*, so
 * a chain longer than this is answered as not-found (and takes a fallback if one
 * was written) rather than exhausting the stack. Cycles are caught far
 * sooner — a cycle is a name already on the stack — so the bound exists for the
 * deep-but-acyclic case, which is the one that must not be answered by a crash.
 */
#define RoboCssVariableMaxDepth 32u

/**
 * Non-zero when @p value contains a `var()` reference anywhere in its tree.
 *
 * A cheap test the caller uses to decide whether substitution is worth doing:
 * most declarations in most documents contain no reference, and copying their
 * tree only to find that out would be work with no answer. A `var()` nested
 * inside a `calc()` or a block is found; an `env()` is **not** (this phase
 * substitutes `var()` and leaves the environment family carried).
 */
int CssValueHasVarReference(const CssValue *value);

/**
 * Substitute every `var()` reference in @p specified and produce @p out.
 *
 * @p customs is the element's own computed style; the custom properties in it —
 * and only they — are the values a reference resolves against. It is borrowed,
 * never mutated, and may be NULL, in which case every reference is undefined.
 *
 * @p out is disposed first and receives a component-value tree that mirrors
 * @p specified: a `var()` node is replaced by the referenced sequence, a
 * `calc(var(--x))` keeps its `calc()` with the reference substituted inside it,
 * and a token that is not part of a reference is copied unchanged. On
 * `RoboNotFound` @p out is left NONE, because there is no half-substituted value.
 *
 * @return RoboOk       @p out holds the substituted tree (`specified` with no
 *                      reference is copied unchanged, which is the identity this
 *                      pass must preserve);
 *         RoboNotFound an undefined reference with no fallback, or a cycle — the
 *                      value is invalid at computed-value time;
 *         RoboInvalidArgument for a NULL @p out;
 *         RoboOutOfMemory when a tree could not be built.
 */
Result CssVarSubstituteValue(const CssValue *specified,
                             const CssComputedStyle *customs,
                             CssValue *out,
                             CssDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_VARIABLE_H */
