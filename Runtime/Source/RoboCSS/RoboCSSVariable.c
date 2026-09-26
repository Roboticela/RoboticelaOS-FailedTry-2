/*
 * RoboCSS custom-property substitution (Phase 18.1) — see RoboCSSVariable.h.
 *
 * One recursive walk, one bounded name stack, and one rule per node kind:
 *
 *   LIST       its children are spliced straight into the destination, because
 *              a list of references is a list of their substitutions and an
 *              extra nesting level would only have to be flattened again;
 *   FUNCTION   cloned with its children substituted, so `calc(var(--x) * 2)`
 *   BLOCK      keeps its own brackets and the reference inside is gone;
 *   var()       resolved to the referenced sequence, or to its fallback, or
 *              reported as invalid at computed-value time;
 *   TOKEN      copied unchanged. A token is never touched — deciding whether a
 *              token is *valid for a property* is the property's own shape's
 *              question and not this pass's.
 */

#include "RoboCSS/RoboCSSVariable.h"

#include "RoboCSSInternal.h"
#include "RoboCSS/RoboCSSToken.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Reading a component value                                           */
/* ------------------------------------------------------------------ */

/** Non-zero when @p token is trivia: whitespace or a comment. */
static int VariableTokenIsTrivia(const CssToken *token)
{
    return token && (token->type == RoboCssTokenWhitespace ||
                     token->type == RoboCssTokenComment);
}

/** Non-zero when @p value is a trivia token value. */
static int VariableIsTriviaValue(const CssValue *value)
{
    return value && value->kind == RoboCssValueKindToken &&
           VariableTokenIsTrivia(&value->token);
}

/** Non-zero when @p value is a `var()` function (ASCII, case-insensitive). */
static int VariableIsVarFunction(const CssValue *value)
{
    if (!value || value->kind != RoboCssValueKindFunction) {
        return 0;
    }
    return CssBytesEqualAsciiCi(value->name.bytes, value->name.length, "var");
}

/**
 * The custom property name a `var()` begins with, or an absent slice.
 *
 * A legal reference starts with one ident-like token whose spelling begins
 * `--`; anything else is malformed and is answered as absent, which the caller
 * treats the same way it treats an undefined name.
 */
static CssSlice VariableNameFromArgs(const CssValue *args)
{
    size_t index;

    for (index = 0u; index < CssValueChildCount(args); index++) {
        const CssValue *child = CssValueChild(args, index);
        CssSlice text;

        if (!child || child->kind != RoboCssValueKindToken ||
            VariableTokenIsTrivia(&child->token)) {
            if (child && child->kind == RoboCssValueKindToken &&
                VariableTokenIsTrivia(&child->token)) {
                continue;
            }
            return CssSliceEmpty();
        }
        if (child->token.type != RoboCssTokenIdent) {
            return CssSliceEmpty();
        }
        text = CssValueTokenText(child);
        if (text.length >= 3u && text.bytes && text.bytes[0] == (uint8_t)'-' &&
            text.bytes[1] == (uint8_t)'-') {
            return text;
        }
        return CssSliceEmpty();
    }
    return CssSliceEmpty();
}

/**
 * Index of the comma that separates the name from a fallback, or (size_t)-1.
 *
 * Only the first comma after the name counts, and it must be the first
 * significant token after the name — `var(--x y)` is malformed rather than a
 * two-name reference, and it is answered exactly like an undefined name.
 */
static size_t VariableFallbackIndex(const CssValue *args)
{
    size_t index;
    int sawName = 0;

    for (index = 0u; index < CssValueChildCount(args); index++) {
        const CssValue *child = CssValueChild(args, index);

        if (!child || child->kind != RoboCssValueKindToken) {
            return (size_t)-1;
        }
        if (VariableTokenIsTrivia(&child->token)) {
            continue;
        }
        if (!sawName) {
            sawName = 1;
            continue;
        }
        if (child->token.type == RoboCssTokenComma) {
            return index;
        }
        return (size_t)-1;
    }
    return (size_t)-1;
}

/* ------------------------------------------------------------------ */
/* The name stack                                                      */
/* ------------------------------------------------------------------ */

typedef struct VariableStack {
    CssSlice names[RoboCssVariableMaxDepth];
    size_t count;
} VariableStack;

static int VariableStackContains(const VariableStack *stack, CssSlice name)
{
    size_t index;

    for (index = 0u; index < stack->count; index++) {
        if (CssSliceEquals(stack->names[index], name)) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Substitution                                                        */
/* ------------------------------------------------------------------ */

static Result VariableResolve(const CssSlice name,
                              const CssComputedStyle *customs,
                              VariableStack *stack,
                              CssValue *out,
                              CssDiag *diag);

static Result VariableAppend(const CssValue *in,
                             const CssComputedStyle *customs,
                             VariableStack *stack,
                             CssValue *dest,
                             CssDiag *diag);

/**
 * Copy the significant children of @p source onto @p dest.
 *
 * Leading and trailing trivia are dropped, which is not tidying: the whitespace
 * around a custom property's value or around a comma-separated fallback is the
 * *separator* that made it spellable, not part of the value. Keeping it would
 * make `var(--x, red)` compute to a value whose text begins with a space, and a
 * consumer comparing or serialising the result would have to know to ignore
 * something the author never wrote as content.
 */
static Result VariableAppendChildren(const CssValue *source,
                                     CssValue *dest,
                                     CssDiag *diag)
{
    size_t first = 0u;
    size_t last = CssValueChildCount(source);
    size_t index;

    while (first < last && VariableIsTriviaValue(CssValueChild(source, first))) {
        first++;
    }
    while (last > first &&
           VariableIsTriviaValue(CssValueChild(source, last - 1u))) {
        last--;
    }
    for (index = first; index < last; index++) {
        CssValue copy = RoboCssCleared;
        Result rc = CssValueCopy(&copy, CssValueChild(source, index), diag);

        if (rc != RoboOk) {
            return rc;
        }
        rc = CssValueAppend(dest, &copy, diag);
        if (rc != RoboOk) {
            CssValueDispose(&copy);
            return rc;
        }
    }
    return RoboOk;
}

/**
 * A `var()` node: the referenced value, else the fallback, else invalid.
 *
 * The resolved custom property is itself substituted before it is spliced in,
 * because a custom property's value may contain another reference — which is
 * what the name stack exists to bound.
 */
static Result VariableAppendVar(const CssValue *node,
                                const CssComputedStyle *customs,
                                VariableStack *stack,
                                CssValue *dest,
                                CssDiag *diag)
{
    CssSlice name = VariableNameFromArgs(node);
    size_t comma = VariableFallbackIndex(node);

    if (!CssSliceIsAbsent(name)) {
        CssValue resolved = RoboCssCleared;
        Result rc = VariableResolve(name, customs, stack, &resolved, diag);

        if (rc == RoboOk) {
            Result appended = VariableAppendChildren(&resolved, dest, diag);

            CssValueDispose(&resolved);
            return appended;
        }
        CssValueDispose(&resolved);
        if (rc != RoboNotFound) {
            return rc;
        }
    }
    if (comma != (size_t)-1) {
        size_t first = comma + 1u;
        size_t last = CssValueChildCount(node);
        size_t index;

        /* The whitespace between the comma and the fallback is the separator
         * that spelled the fallback, not part of it. */
        while (first < last && VariableIsTriviaValue(CssValueChild(node, first))) {
            first++;
        }
        while (last > first &&
               VariableIsTriviaValue(CssValueChild(node, last - 1u))) {
            last--;
        }
        for (index = first; index < last; index++) {
            Result rc = VariableAppend(CssValueChild(node, index), customs,
                                       stack, dest, diag);

            if (rc != RoboOk) {
                return rc;
            }
        }
        return RoboOk;
    }
    return RoboNotFound;
}

static Result VariableAppend(const CssValue *in,
                             const CssComputedStyle *customs,
                             VariableStack *stack,
                             CssValue *dest,
                             CssDiag *diag)
{
    size_t index;

    if (!in || in->kind == RoboCssValueKindNone) {
        return RoboOk;
    }
    if (VariableIsVarFunction(in)) {
        return VariableAppendVar(in, customs, stack, dest, diag);
    }
    if (in->kind == RoboCssValueKindList) {
        /* Flatten: a list of references *is* the list of what they resolve to,
         * so an extra nesting level would only have to be undone downstream. */
        for (index = 0u; index < CssValueChildCount(in); index++) {
            Result rc = VariableAppend(CssValueChild(in, index), customs,
                                       stack, dest, diag);

            if (rc != RoboOk) {
                return rc;
            }
        }
        return RoboOk;
    }
    if (CssValueKindHoldsChildren(in->kind)) {
        CssValue clone = RoboCssCleared;
        Result rc;

        if (in->kind == RoboCssValueKindFunction) {
            rc = CssValueMakeFunction(&clone, in->name, in->range, diag);
        } else {
            rc = CssValueMakeBlock(&clone, in->open, in->range, diag);
        }
        if (rc != RoboOk) {
            return rc;
        }
        for (index = 0u; index < CssValueChildCount(in); index++) {
            rc = VariableAppend(CssValueChild(in, index), customs, stack,
                                &clone, diag);
            if (rc != RoboOk) {
                CssValueDispose(&clone);
                return rc;
            }
        }
        rc = CssValueAppend(dest, &clone, diag);
        if (rc != RoboOk) {
            CssValueDispose(&clone);
        }
        return rc;
    }
    {
        CssValue copy = RoboCssCleared;
        Result rc = CssValueCopy(&copy, in, diag);

        if (rc != RoboOk) {
            return rc;
        }
        rc = CssValueAppend(dest, &copy, diag);
        if (rc != RoboOk) {
            CssValueDispose(&copy);
        }
        return rc;
    }
}

/**
 * The value of the custom property @p name, substituted, as a list.
 *
 * Absent, cyclic and too-deep all answer `RoboNotFound`, which is exactly the
 * fact the caller needs: all three mean "this reference has no value here", so
 * the fallback (if any) is the right thing to use and the declaration is
 * invalid at computed-value time otherwise.
 */
static Result VariableResolve(const CssSlice name,
                              const CssComputedStyle *customs,
                              VariableStack *stack,
                              CssValue *out,
                              CssDiag *diag)
{
    size_t found;
    const CssComputedValue *value;
    const CssValue *preserved;
    Result rc;

    CssValueInit(out);
    if (CssSliceIsAbsent(name) || !customs) {
        return RoboNotFound;
    }
    if (VariableStackContains(stack, name) ||
        stack->count >= RoboCssVariableMaxDepth) {
        return RoboNotFound;
    }
    found = CssComputedStyleFind(customs, name);
    if (found == (size_t)-1) {
        return RoboNotFound;
    }
    value = CssComputedStyleComputedByName(customs, name);
    preserved = value ? CssComputedValuePreserved(value) : NULL;

    rc = CssValueMakeList(out, diag);
    if (rc != RoboOk) {
        return rc;
    }
    stack->names[stack->count++] = name;
    if (preserved) {
        rc = VariableAppend(preserved, customs, stack, out, diag);
    }
    stack->count--;
    if (rc != RoboOk) {
        CssValueDispose(out);
        return rc;
    }
    return RoboOk;
}

int CssValueHasVarReference(const CssValue *value)
{
    size_t index;

    if (!value) {
        return 0;
    }
    if (VariableIsVarFunction(value)) {
        return 1;
    }
    for (index = 0u; index < CssValueChildCount(value); index++) {
        if (CssValueHasVarReference(CssValueChild(value, index))) {
            return 1;
        }
    }
    return 0;
}

Result CssVarSubstituteValue(const CssValue *specified,
                             const CssComputedStyle *customs,
                             CssValue *out,
                             CssDiag *diag)
{
    VariableStack stack;
    Result rc;

    if (!out) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL");
    }
    CssValueDispose(out);
    CssValueInit(out);
    stack.count = 0u;
    rc = CssValueMakeList(out, diag);
    if (rc != RoboOk) {
        return rc;
    }
    rc = VariableAppend(specified, customs, &stack, out, diag);
    if (rc != RoboOk) {
        CssValueDispose(out);
        return rc;
    }
    return RoboOk;
}
