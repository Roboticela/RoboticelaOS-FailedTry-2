/*
 * RoboCSS selector parsing (Phase 9.4).
 *
 * Phase 9.3 deliberately left a style rule's `selectors` empty and kept its
 * prelude as component values, so that "was this rule's prelude understood as
 * selectors yet" would be answerable by asking. This header is the thing that
 * answers it.
 *
 *   CSS source
 *       ↓
 *   RoboCSSTokenizer (9.2)          bytes → tokens, escapes and all
 *       ↓
 *   the selector grammar (here)     tokens → selector lists
 *       ↓
 *   CssSelectorList (9.1)  simple / compound / complex / list
 *
 * It lexes nothing itself. Every byte arrives as a 9.2 token, and every name it
 * records is the token's *value* — escapes already resolved by the tokenizer's
 * own rule — copied once into the selector list's name arena. So `.foo\:bar`
 * carries the six bytes `foo:bar`, and a matcher never has to know that escapes
 * exist. That is also why this header has a rule of its own:
 *
 *     A parsed selector's `name`, `attribute` and `attributeValue` slices
 *     point into the *list's* arena, not into the source. They are valid until
 *     the list is disposed, and they stay valid across appends, because the
 *     parser reserves the arena up front from the text's length.
 *
 * What is *not* here: matching (RoboCSSMatcher.h), specificity, and the
 * cascade. A parsed selector list is a description of what a rule applies to,
 * not a decision about anything.
 *
 * Errors
 *   A selector list is all-or-nothing, as CSS says it is: one bad selector in a
 *   list makes the whole list invalid, which is the rule that makes `:is()`
 *   worth having. So these calls return RoboParseError when the text is not a
 *   valid selector list, leave @p out empty, and always fill @p outError with
 *   the first problem — a real position and a message naming what was expected.
 *   @p outError is filled on success too (kind NONE), so a caller that never
 *   checks a result code still cannot be handed a record describing a previous
 *   call.
 *
 *   @p out is *initialised first*, the same rule the 9.3 grammar follows: a
 *   caller may hand over an unused or cleared record. Re-parsing into a list that
 *   is already live is the caller's own dispose, because only the caller knows
 *   whether the selectors it is holding are still wanted.
 *
 *   Nothing here fails the process, allocates unboundedly, or recurses
 *   unboundedly: `:not()` nesting is capped by RoboCssMaxSelectorNesting,
 *   and past the cap the selector is rejected rather than parsed more deeply.
 */

#ifndef ROBO_CSS_SELECTOR_PARSER_H
#define ROBO_CSS_SELECTOR_PARSER_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSError.h"
#include "RoboCSS/RoboCSSRule.h"
#include "RoboCSS/RoboCSSSelector.h"
#include "RoboCSS/RoboCSSSource.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Selector lists                                                      */
/* ------------------------------------------------------------------ */

/**
 * Parse @p text as a selector list — the text of a style rule's prelude, or an
 * argument to a selector-taking API.
 *
 * @p out is initialised first and is left empty on failure. The recorded names
 * borrow @p out's arena (not @p text), and the source ranges start at 0, because
 * @p text *is* the coordinate system. @p text need not outlive @p out; it does
 * need to be readable for the duration of the call.
 *
 * @return RoboOk; RoboParseError when @p text is not a valid selector list;
 *         RoboInvalidArgument for a NULL output or a NULL/zero-length text;
 *         RoboOutOfMemory.
 */
Result CssSelectorListParseText(CssSelectorList *out,
                                                const char *text,
                                                CssError *outError,
                                                CssDiag *diag);

/** As above, over @p length bytes at @p bytes. */
Result CssSelectorListParseSlice(CssSelectorList *out,
                                                 CssSlice text,
                                                 CssError *outError,
                                                 CssDiag *diag);

/**
 * Parse @p range of @p source as a selector list.
 *
 * Ranges and error positions are source offsets, which is what a stylesheet's
 * own diagnostics want: a rule's preludeRange fed here produces errors located
 * where the author actually wrote them.
 *
 * @return as CssSelectorListParseText; RoboInvalidArgument for a NULL
 *         source or a range that does not lie within it.
 */
Result CssSelectorListParseRange(CssSelectorList *out,
                                                 const CssSource *source,
                                                 CssRange range,
                                                 CssError *outError,
                                                 CssDiag *diag);

/**
 * Turn @p rule's prelude into its selectors.
 *
 * This is the one call that closes Phase 9.3's open question, and it is
 * deliberately the only one: a rule's `selectors` is whatever its
 * `preludeValues` describe, and there is no second way to fill it.
 *
 * On success @p rule's selector list is replaced — the previous one is disposed
 * — so calling this twice is idempotent and cannot accumulate. On failure the
 * rule keeps an empty selector list and is otherwise untouched: a rule whose
 * prelude is not a selector list is still a rule, and Phase 9.3 already decided
 * that malformed CSS produces a model rather than a failed parse.
 *
 * @p source must be the source the rule was parsed from; passing another one
 * produces a diagnosis of different bytes rather than a crash, because the
 * prelude range is bounds-checked against @p source.
 *
 * @return RoboOk; RoboParseError when the prelude is not a valid selector
 *         list; RoboInvalidArgument; RoboOutOfMemory.
 */
Result CssStyleRuleParseSelectors(CssStyleRule *rule,
                                                  const CssSource *source,
                                                  CssError *outError,
                                                  CssDiag *diag);

/* ------------------------------------------------------------------ */
/* An+B                                                               */
/* ------------------------------------------------------------------ */

/**
 * An `An+B` expression: the argument of `:nth-child()` and its family.
 *
 * `n` ranges over the non-negative integers, so the matched indexes are
 * `a·n + b` for n = 0, 1, 2, … — which is why `a` may be negative (the sequence
 * then ends) and why the pair is not reducible to a list of indexes without
 * knowing a bound.
 */
typedef struct CssAnPlusB {
    /** The coefficient of `n`; 0 for a bare integer such as `3`. */
    int a;
    /** The offset; the whole expression when `a` is 0. */
    int b;
} CssAnPlusB;

/**
 * Parse @p length bytes at @p text as An+B.
 *
 * Accepts the forms CSS defines: `odd`, `even`, an integer (`3`, `-1`, `+2`),
 * `n`, `-n`, `+n`, and an `n` form with an optional sign and offset
 * (`2n+1`, `-3n-2`, `n-1`, `2n + 1`, `n`). Whitespace is allowed around the
 * sign and around the whole expression, as the microsyntax allows; nothing else
 * is, so `2n+`, `n*n` and `2 n` are rejected rather than guessed at.
 *
 * @return RoboOk with @p out filled; RoboParseError for anything else;
 *         RoboInvalidArgument for a NULL output or empty text.
 */
Result CssAnPlusBParse(const char *text,
                                       size_t length,
                                       CssAnPlusB *out,
                                       CssDiag *diag);

/**
 * Non-zero when 1-based @p index is in @p form's sequence: some n >= 0 with
 * `a·n + b == index`.
 *
 * @p index must be 1 or greater — `:nth-child()` counts elements, and there is
 * no element 0 — so 0 and negatives are false for every form. `a == 0` is the
 * single-index case; a negative `a` is a finite sequence whose members are the
 * non-negative ones.
 */
int CssAnPlusBMatches(const CssAnPlusB *form, int index);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_SELECTOR_PARSER_H */
